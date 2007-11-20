/*
   (c) Copyright 2003-2007, Hewlett-Packard Development Company, LP

   See the file named COPYING for license details
*/

#include <ctype.h>

#include <Lintel/StringUtil.H>

#include "common.hpp"

using namespace std;

void
fh2mountData::pruneToMountPart(string &adjust)
{
    if (adjust.size() == 32) {
	// experimentally, the remaining bytes seem to be the unchanging
	// bits of NetApp filehandles
	INVARIANT(adjust.size() >= 20, "bad");
	((u_int32_t *)&(adjust[0]))[3] = 0;
	((u_int32_t *)&(adjust[0]))[4] = 0;
    }
}

std::string
fh2mountData::pruneToMountPart(const ConstantString &from)
{
    std::string ret(from.data(), from.size());
    pruneToMountPart(ret);
    return ret;
}

bool
fh2mountData::equalMountParts(const string &v1, const string &v2)
{
    if (v1.size() != v2.size() || v1.size() != 32)
	return false;
    const u_int32_t *a = (const u_int32_t *)v1.data();
    const u_int32_t *b = (const u_int32_t *)v2.data();
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[5] == b[5] && a[6] == b[6] && a[7] == b[7];
}

struct fh2fnData {
    ConstantString filehandle;
    string filename;
    void set(const ConstantString &a, const string &b) { filehandle = a; filename = b;}
};

class fh2fnHash {
public:
    unsigned int operator()(const fh2fnData &k) {
	return HashTable_hashbytes(k.filehandle.data(),k.filehandle.size());
    }
};

class fh2fnEqual {
public:
    bool operator()(const fh2fnData &a, const fh2fnData &b) {
	return a.filehandle == b.filehandle;
    }
};

HashTable<fh2fnData, fh2fnHash, fh2fnEqual> fh2fn;
static bool did_fh2fn_insert = false;

string *
fnByFileHandle(const ConstantString &fh)
{
    AssertAlways(did_fh2fn_insert,("bad"));
    fh2fnData k;
    k.filehandle = fh;
    fh2fnData *v = fh2fn.lookup(k);
    if (v == NULL) 
	return NULL;
    else
	return &v->filename;
}

class FillFH2FN_HashTable : public NFSDSModule {
public:
    FillFH2FN_HashTable(DataSeriesModule &_source)
	: source(_source), s(ExtentSeries::typeExact),
	  filename(s,"filename",Field::flag_nullable),
	  filehandle(s,"filehandle"),uniquecount(0)
    { }
    virtual ~FillFH2FN_HashTable() {}
    DataSeriesModule &source;
    ExtentSeries s;
    Variable32Field filename, filehandle;
    int uniquecount;

    Extent *getExtent() {
	did_fh2fn_insert = true;
	Extent *e = source.getExtent();
	if (e == NULL) return NULL;
	fh2fnData v;
	for(s.setExtent(e);s.pos.morerecords();++s.pos) {
	    if (filename.isNull())
		continue;
	    AssertAlways(filename.size() > 0,("??"));
	    v.set(filehandle.stringval(),filename.stringval());
	    fh2fnData *v2 = fh2fn.lookup(v);
	    if (v2 != NULL) {
		if (v2->filename != v.filename) {
		    if (false) {
			fprintf(stderr,"Warning, filehandle %s changed names from %s to %s\n",
				hexstring(v.filehandle).c_str(),maybehexstring(v2->filename).c_str(),maybehexstring(v.filename).c_str());
		    }
		    v2->filename = v.filename;
		}
	    } else {
		if (false && v.filename == "xxxx") // [0] == 'm' && v.filename[5] == 'p' && v.filename[6] == 'm') 
		    printf("add #%d %s/%x -> %s\n",uniquecount,
			   hexstring(v.filehandle).c_str(),
			   *(unsigned int *)(v.filehandle.data() + 12),
			   v.filename.c_str());
		fh2fn.add(v);
		++uniquecount;
	    }
	}
	return e;
    }
    virtual void printResult() { }
};

NFSDSModule *
NFSDSAnalysisMod::newFillFH2FN_HashTable(DataSeriesModule &source)
{
    return new FillFH2FN_HashTable(source);
}

fh2mountT fh2mount;

string maybehexstring(const ConstantString &in)
{
    string tmp(in.data(), in.size());
    return maybehexstring(tmp);
}
    
string hexstring(const ConstantString &in)
{
    string tmp(in.data(), in.size());
    return hexstring(tmp);
}

class FillMount_HashTable : public NFSDSModule {
public:
    FillMount_HashTable(DataSeriesModule &_source)
	: source(_source), s(ExtentSeries::typeExact),
	  server(s,"server"),
	  pathname(s,"pathname"),
	  filehandle(s,"filehandle")
    { }
    virtual ~FillMount_HashTable() {}
    DataSeriesModule &source;
    ExtentSeries s;
    Int32Field server;
    Variable32Field pathname, filehandle;

    Extent *getExtent() {
	Extent *e = source.getExtent();
	if (e == NULL) return NULL;
	for(s.setExtent(e);s.pos.morerecords();++s.pos) {
	    if (fh2mount.size() > NFSDSAnalysisMod::max_mount_points_expected) {
		break;
	    }

	    fh2mountData v(filehandle.stringval(), pathname.stringval(),
			   server.val());
	    fh2mountData *d = fh2mount.lookup(v);
	    if (d == NULL) {
		if (false) {
		    printf("mount %s: %s -> %s\n",ipv4tostring(server.val()).c_str(),
			   maybehexstring(pathname.stringval()).c_str(),
			   maybehexstring(filehandle.stringval()).c_str());
		}
		if (v.fullfh.size() != 32) {
		    if (false) printf("odd, non32 byte filehandle from mount!\n");
		}
		fh2mount.add(v);
	    } else if (d->pathname != v.pathname) {
 	        fprintf(stderr,"filehandle %s has two mount paths %s,%s\n",
			hexstring(filehandle.stringval()).c_str(),
			hexstring(d->pathname).c_str(),hexstring(v.pathname).c_str());
	    }
	}
	return e;
    }
    virtual void printResult() { }
};

NFSDSModule *
NFSDSAnalysisMod::newFillMount_HashTable(DataSeriesModule &source)
{
    return new FillMount_HashTable(source);
}