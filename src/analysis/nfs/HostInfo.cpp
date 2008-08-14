#include <vector>
#include <bitset>

#include <boost/bind.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/tuple/tuple_io.hpp>

#include <analysis/nfs/common.hpp>
#include <analysis/nfs/HostInfo.hpp>

#include <Lintel/HashMap.hpp>

#include <DataSeries/GeneralField.hpp>

using namespace std;
using boost::format;

namespace boost { namespace tuples {
    inline uint32_t hash(const null_type &) { return 0; }
    inline uint32_t hash(const bool a) { return a; }
    inline uint32_t hash(const int32_t a) { return a; }

    template<class Head>
    inline uint32_t hash(const cons<Head, null_type> &v) {
	return hash(v.get_head());
    }

    // See http://burtleburtle.net/bob/c/lookup3.c for a discussion
    // about how we could have even fewer calls to the mix function.
    // Would want to upgrade to the newer hash function.
    template<class Head1, class Head2, class Tail>
    inline uint32_t hash(const cons<Head1, cons<Head2, Tail> > &v) {
	uint32_t a = hash(v.get_head());
	uint32_t b = hash(v.get_tail().get_head());
	uint32_t c = hash(v.get_tail().get_tail());
	return BobJenkinsHashMix3(a,b,c);
    }

    template<class BitSet>
    inline uint32_t partial_hash(const null_type &, const BitSet &, size_t) {
	return 0;
    }

    template<class Head, class BitSet>
    inline uint32_t partial_hash(const cons<Head, null_type> &v,
				 const BitSet &used, size_t cur_pos) {
	return used[cur_pos] ? hash(v.get_head()) : 0;
    }

    template<class Head1, class Head2, class Tail, class BitSet>
    inline uint32_t partial_hash(const cons<Head1, cons<Head2, Tail> > &v, 
				 const BitSet &used, size_t cur_pos) {
	uint32_t a = used[cur_pos] ? hash(v.get_head()) : 0;
	uint32_t b = used[cur_pos+1] ? hash(v.get_tail().get_head()) : 0;
	uint32_t c = partial_hash(v.get_tail().get_tail(), used, cur_pos + 2);
	return BobJenkinsHashMix3(a,b,c);
    }

    template<class BitSet>
    inline bool partial_equal(const null_type &lhs, const null_type &rhs,
			      const BitSet &used, size_t cur_pos) {
	return true;
    }

    template<class Head, class Tail, class BitSet>
    inline bool partial_equal(const cons<Head, Tail> &lhs,
			      const cons<Head, Tail> &rhs,
			      const BitSet &used, size_t cur_pos) {
	if (used[cur_pos] && lhs.get_head() != rhs.get_head()) {
	    return false;
	}
	return partial_equal(lhs.get_tail(), rhs.get_tail(), 
			     used, cur_pos + 1);
    }

    template<class BitSet>
    inline bool partial_strict_less_than(const null_type &lhs, 
					 const null_type &rhs,
					 const BitSet &used_lhs, 
					 const BitSet &used_rhs,
					 size_t cur_pos) {
	return false;
    }

    template<class Head, class Tail, class BitSet>
    inline bool partial_strict_less_than(const cons<Head, Tail> &lhs,
					 const cons<Head, Tail> &rhs,
					 const BitSet &used_lhs, 
					 const BitSet &used_rhs,
					 size_t cur_pos) {
	if (used_lhs[cur_pos] && used_rhs[cur_pos]) {
	    if (lhs.get_head() < rhs.get_head()) {
		return true;
	    } else if (lhs.get_head() > rhs.get_head()) {
		return false;
	    } else {
		// don't know, fall through to recursion.
	    }
	} else if (used_lhs[cur_pos] && !used_rhs[cur_pos]) {
	    return true; // used < *
	} else if (!used_lhs[cur_pos] && used_rhs[cur_pos]) {
	    return false; // * > used
	} 
	return partial_strict_less_than(lhs.get_tail(), rhs.get_tail(), 
					used_lhs, used_rhs, cur_pos + 1);
    }
} }

template<class Tuple> struct TupleHash {
    uint32_t operator()(const Tuple &a) {
	return boost::tuples::hash(a);
    }
};

template<class Tuple> struct PartialTuple {
    BOOST_STATIC_CONSTANT(uint32_t, 
			  length = boost::tuples::length<Tuple>::value);

    Tuple data;
    typedef bitset<boost::tuples::length<Tuple>::value> UsedT;
    UsedT used;

    PartialTuple() { }
    PartialTuple(const Tuple &from) : data(from) { }

    bool operator==(const PartialTuple &rhs) const {
	if (used != rhs.used) { 
	    return false;
	}
	return boost::tuples::partial_equal(data, rhs.data, used, 0);
    }
    bool operator<(const PartialTuple &rhs) const {
	return boost::tuples::partial_strict_less_than(data, rhs.data, 
						       used, rhs.used, 0);
    }
};

template<class Tuple> struct PartialTupleHash {
    uint32_t operator()(const PartialTuple<Tuple> &v) {
	return boost::tuples::partial_hash(v.data, v.used, 0);
    }
};

template<class Tuple>
class StatsCube {
public:
    typedef HashMap<Tuple, Stats *, TupleHash<Tuple> > CubeMap;
    typedef typename CubeMap::iterator CMIterator;

    typedef vector<typename CubeMap::value_type> CMValueVector;
    typedef typename CMValueVector::iterator CMVVIterator;

    typedef PartialTuple<Tuple> MyPartial;
    typedef HashMap<MyPartial, Stats *, PartialTupleHash<Tuple> > 
        PartialTupleCubeMap;
    typedef typename PartialTupleCubeMap::iterator PTCMIterator;
    typedef vector<typename PartialTupleCubeMap::value_type> PTCMValueVector;
    typedef typename PTCMValueVector::iterator PTCMVVIterator;

    typedef boost::function<Stats *()> StatsFactoryFn;
    typedef boost::function<void (Tuple &key, Stats *value)> PrintBaseFn;
    typedef boost::function<void (Tuple &key, typename MyPartial::UsedT, 
				  Stats *value)> PrintCubeFn;

    StatsCube(const StatsFactoryFn fn) : stats_factory_fn(fn) { }

    void add(const Tuple &key, const Stats &value) {
	getCubeEntry(key).add(value);
    }

    void add(const Tuple &key, double value) {
	getCubeEntry(key).add(value);
    }

    void cube() {
	for(CMIterator i = base_data.begin(); i != base_data.end(); ++i) {
	    MyPartial tmp_key(i->first);
	    cubeAddOne(tmp_key, 0, false, *i->second);
	}
    }

    void cubeAddOne(MyPartial &key, size_t pos, bool had_false, 
		    const Stats &value) {
	if (pos == key.length) {
	    if (had_false) {
		getPartialEntry(key).add(value);
	    }
	} else {
	    DEBUG_SINVARIANT(pos < key.length);
	    key.used[pos] = true;
	    cubeAddOne(key, pos + 1, had_false, value);
	    key.used[pos] = false;
	    cubeAddOne(key, pos + 1, true, value);
	}
    }

    void printBase(const PrintBaseFn fn) {
	CMValueVector sorted;

	// TODO: figure out why the below doesn't work.
	//	sorted.push_back(base_data.begin(), base_data.end());
	for(CMIterator i = base_data.begin(); i != base_data.end(); ++i) {
	    sorted.push_back(*i);
	}
	sort(sorted.begin(), sorted.end());

	for(CMVVIterator i = sorted.begin(); i != sorted.end(); ++i) {
	    fn(i->first, i->second);
	}
    }

    void printCube(const PrintCubeFn fn) {
	PTCMValueVector sorted;

	for(PTCMIterator i = cube_data.begin(); i != cube_data.end(); ++i) {
	    sorted.push_back(*i);
	}
	sort(sorted.begin(), sorted.end());

	for(PTCMVVIterator i = sorted.begin(); i != sorted.end(); ++i) {
	    fn(i->first.data, i->first.used, i->second);
	}
    }
private:
    Stats &getCubeEntry(const Tuple &key) {
	Stats * &v = base_data[key];

	if (v == NULL) {
	    v = stats_factory_fn();
	}
	return *v;
    }

    Stats &getPartialEntry(const MyPartial &key) {
	Stats * &v = cube_data[key];

	if (v == NULL) {
	    v = stats_factory_fn();
	}
	return *v;
    }

    StatsFactoryFn stats_factory_fn;
    CubeMap base_data;
    PartialTupleCubeMap cube_data;
    uint32_t foo;
};

// We do the rollup internal to this module rather than an external
// program so if we switch to using statsquantile the rollup will
// still work correctly.

// To graph:
// create a table like: create table nfs_perf (sec int not null, ops double not null);
// perl -ane 'next unless $F[0] eq "*" && $F[1] =~ /^\d+$/o && $F[2] eq "send" && $F[3] eq "*" && $F[4] eq "*";print "insert into nfs_perf values ($F[1], $F[5]);\n";' < output of nfsdsanalysis -l ## run. | mysql test
// use mercury-plot (from Lintel) to graph, for example:
/*
unplot
plotwith * lines
plot 3600*round((sec - min_sec)/3600,0) as x, avg(ops/(2*60.0)) as y from nfs_perf, (select min(sec) as min_sec from nfs_perf) as ms group by round((sec - min_sec)/3600,0)
plottitle _ mean ops/s 
plot 3600*round((sec - min_sec)/3600,0) as x, max(ops/(2*60.0)) as y from nfs_perf, (select min(sec) as min_sec from nfs_perf) as ms group by round((sec - min_sec)/3600,0)
plottitle _ max ops/s
plot 3600*round((sec - min_sec)/3600,0) as x, min(ops/(2*60.0)) as y from nfs_perf, (select min(sec) as min_sec from nfs_perf) as ms group by round((sec - min_sec)/3600,0)
plottitle _ min ops/s
gnuplot set xlabel "seconds"
gnuplot set ylabel "operations (#requests+#replies)/2 per second"
pngplot set-18.png
*/

namespace {
    static const string str_send("send");
    static const string str_recv("recv");
    static const string str_request("request");
    static const string str_response("response");
    static const string str_star("*");
}

class HostInfo : public RowAnalysisModule {
public:
    HostInfo(DataSeriesModule &_source, const std::string &arg) 
	: RowAnalysisModule(_source),
	  packet_at(series, ""),
	  payload_length(series, ""),
	  op_id(series, "", Field::flag_nullable),
	  nfs_version(series, "", Field::flag_nullable),
	  source_ip(series,"source"), 
	  dest_ip(series, "dest"),
	  is_request(series, ""),
	  cube(boost::bind(&HostInfo::makeStats))
    {
	group_seconds = stringToUInt32(arg);
	host_to_data.reserve(5000);
    }

    virtual ~HostInfo() { }

    //                   host,   is_send, seconds, operation, is_request
    typedef boost::tuple<int32_t, bool,   int32_t,  uint8_t,    bool> Tuple;
    typedef bitset<5> Used;
    static int32_t host(const Tuple &t) {
	return t.get<0>();
    }
    static string host(const Tuple &t, const Used &used) {
	if (used[0] == false) {
	    return str_star;
	} else {
	    return str(format("%08x") % host(t));
	}
    }
    static bool isSend(const Tuple &t) {
	return t.get<1>();
    }
    static const string &isSendStr(const Tuple &t) {
	return isSend(t) ? str_send : str_recv;
    }

    static string isSendStr(const Tuple &t, const Used &used) {
	if (used[1] == false) {
	    return str_star;
	} else {
	    return isSendStr(t);
	}
    }

    static int32_t seconds(const Tuple &t) {
	return t.get<2>();
    }

    static string seconds(const Tuple &t, const Used &used) {
	if (used[2] == false) {
	    return str_star;
	} else {
	    return str(format("%d") % seconds(t));
	}
    }

    static uint8_t operation(const Tuple &t) {
	return t.get<3>();
    }

    static const string &operationStr(const Tuple &t) {
	return unifiedIdToName(operation(t));
    }
    static string operationStr(const Tuple &t, const Used &used) {
	if (used[3] == false) {
	    return str_star;
	} else {
	    return operationStr(t);
	}
    }

    static bool isRequest(const Tuple &t) {
	return t.get<4>();
    }
    static const string &isRequestStr(const Tuple &t) {
	return isRequest(t) ? str_request : str_response;
    }

    static const string &isRequestStr(const Tuple &t, const Used &used) {
	if (used[4] == false) {
	    return str_star;
	} else {
	    return isRequestStr(t);
	}
    }

    void newExtentHook(const Extent &e) {
	if (series.getType() != NULL) {
	    return; // already did this
	}
	const ExtentType &type = e.getType();
	if (type.getName() == "NFS trace: common") {
	    SINVARIANT(type.getNamespace() == "" &&
		       type.majorVersion() == 0 &&
		       type.minorVersion() == 0);
	    packet_at.setFieldName("packet-at");
	    payload_length.setFieldName("payload-length");
	    op_id.setFieldName("op-id");
	    nfs_version.setFieldName("nfs-version");
	    is_request.setFieldName("is-request");
	} else if (type.getName() == "Trace::NFS::common"
		   && type.versionCompatible(1,0)) {
	    packet_at.setFieldName("packet-at");
	    payload_length.setFieldName("payload-length");
	    op_id.setFieldName("op-id");
	    nfs_version.setFieldName("nfs-version");
	    is_request.setFieldName("is-request");
	} else if (type.getName() == "Trace::NFS::common"
		   && type.versionCompatible(2,0)) {
	    packet_at.setFieldName("packet_at");
	    payload_length.setFieldName("payload_length");
	    op_id.setFieldName("op_id");
	    nfs_version.setFieldName("nfs_version");
	    is_request.setFieldName("is_request");
	} else {
	    FATAL_ERROR("?");
	}
    }

    struct TotalTime {
	vector<Stats *> total;
	uint32_t first_time_seconds;
	uint32_t group_seconds;

	TotalTime(uint32_t a, uint32_t b) : first_time_seconds(a), group_seconds(b) { }
	
	void add(Stats &ent, uint32_t seconds) {
	    SINVARIANT(seconds % group_seconds == 0);
	    SINVARIANT(seconds >= first_time_seconds);
	    uint32_t offset = (seconds - first_time_seconds)/group_seconds;
	    if (total.size() <= offset) {
		total.resize(offset+1);
	    }
	    if (total[offset] == NULL) {
		total[offset] = new Stats();
	    }
	    total[offset]->add(ent);
	}

	void print() {
	    uint32_t cur_seconds = first_time_seconds;
	    Stats complete_total;
	    for(vector<Stats *>::iterator i = total.begin(); i != total.end(); ++i) {
		if (*i != NULL) {
		    cout << format("       * %10d  send           *        * %6lld %8.2f\n")
			% cur_seconds % (**i).countll() % (**i).mean();
		    complete_total.add(**i);
		}
		
		cur_seconds += group_seconds;
	    }
	    cout << format("       *          *  send           *        * %6lld %8.2f\n")
		% complete_total.countll() % complete_total.mean();
	}
    };

    struct PerDirectionData {
	vector<Stats *> req_data, resp_data;
	void add(unsigned op_id, bool is_request, uint32_t bytes) {
	    doAdd(op_id, bytes, is_request ? req_data : resp_data);
	}

	void print(uint32_t host_id, uint32_t seconds,
		   const std::string &direction,
		   Stats &total_req, Stats &total_resp,
		   TotalTime *total_time) {
	    doPrint(host_id, seconds, direction, "request", req_data, 
		    total_req, total_time);
	    doPrint(host_id, seconds, direction, "response", resp_data, 
		    total_resp, total_time);
	}

    private:
	void doPrint(uint32_t host_id, uint32_t seconds, 
		     const std::string &direction, 
		     const std::string &msgtype,
		     const vector<Stats *> &data,
		     Stats &bigger_total, TotalTime *total_time) {
	    Stats total;
	    for(unsigned i = 0; i < data.size(); ++i) {
		if (data[i] == NULL) {
		    continue;
		}
		total.add(*data[i]);
		cout << format("%08x %10d %s %12s %8s %6lld %8.2f\n")
		    % host_id % seconds % direction % unifiedIdToName(i) 
		    % msgtype % data[i]->countll() % data[i]->mean();
	    }
	    if (total.countll() > 0) {
		cout << format("%08x %10d %s            * %8s %6lld %8.2f\n")
		    % host_id % seconds % direction 
		    % msgtype % total.countll() % total.mean();
		bigger_total.add(total);
		if (total_time != NULL) {
		    total_time->add(total, seconds);
		}
	    }
	}

	void doAdd(unsigned op_id, uint32_t bytes, vector<Stats *> &data) {
	    if (data.size() <= op_id) {
		data.resize(op_id+1);
	    }
	    if (data[op_id] == NULL) {
		data[op_id] = new Stats;
	    }
	    data[op_id]->add(bytes);
	}

    };

    struct PerTimeData {
	PerDirectionData send, recv;
    };

    struct PerHostData {
	PerHostData() 
	    : first_time_seconds(0) {}
	vector<PerTimeData *> time_entries;
	uint32_t first_time_seconds; 
	PerTimeData &getPerTimeData(uint32_t seconds, uint32_t group_seconds) {
	    seconds -= seconds % group_seconds;
	    if (time_entries.empty()) {
		first_time_seconds = seconds;
	    }
	    SINVARIANT(seconds >= first_time_seconds);
	    uint32_t entry = (seconds - first_time_seconds) / group_seconds;
	    INVARIANT(entry < 100000, format("time range of %d..%d seconds grouped every %d seconds leads to more than 100,000 entries; this is probably not what you want") % first_time_seconds % seconds % group_seconds);
	    if (entry >= time_entries.size()) {
		time_entries.resize(entry+1);
	    }
	    DEBUG_SINVARIANT(entry < time_entries.size());
	    if (time_entries[entry] == NULL) {
		time_entries[entry] = new PerTimeData();
	    }
	    return *time_entries[entry];
	}

	void printTotal(Stats &total, uint32_t host_id,
			const std::string &direction,
			const std::string &msgtype) {
	    if (total.countll() > 0) {
		cout << format("%08x          * %s            * %8s %6lld %8.2f\n")
		    % host_id % direction % msgtype % total.countll() 
		    % total.mean();
	    }
	}

	void print(uint32_t host_id, uint32_t group_seconds, TotalTime &total_time) {
	    uint32_t start_seconds = first_time_seconds;
	    // Caches act as both senders and recievers.
	    Stats total_send_req, total_send_resp, 
		total_recv_req, total_recv_resp;
	    for(vector<PerTimeData *>::iterator i = time_entries.begin();
		i != time_entries.end(); ++i, start_seconds += group_seconds) {
		if (*i != NULL) {
		    (**i).send.print(host_id, start_seconds, "send", 
				     total_send_req, total_send_resp, &total_time);
		    (**i).recv.print(host_id, start_seconds, "recv", 
				     total_recv_req, total_recv_resp, NULL);
		}
	    }
	    printTotal(total_send_req,  host_id, "send", "request");
	    printTotal(total_send_resp, host_id, "send", "response");
	    printTotal(total_recv_req,  host_id, "recv", "request");
	    printTotal(total_recv_resp, host_id, "recv", "response");
	}
    };

    HashMap<uint32_t, PerHostData> host_to_data;

    virtual void processRow() {
#if 1
	uint32_t seconds = packet_at.valSec();
	seconds -= seconds % group_seconds;
	uint8_t operation = opIdToUnifiedId(nfs_version.val(), op_id.val());

	// sender...
	Tuple cube_key(source_ip.val(), true, 
		       seconds, operation, is_request.val());
	cube.add(cube_key, payload_length.val());
	// reciever...
	cube_key.get<0>() = dest_ip.val();
	cube_key.get<1>() = false;
	cube.add(cube_key, payload_length.val());
#else
	uint32_t seconds = packet_at.valSec();
	unsigned unified_op_id = opIdToUnifiedId(nfs_version.val(),
						 op_id.val());
	host_to_data[source_ip.val()]
	    .getPerTimeData(seconds, group_seconds)
	    .send.add(unified_op_id, is_request.val(), payload_length.val());
	host_to_data[dest_ip.val()]
	    .getPerTimeData(seconds, group_seconds)
	    .recv.add(unified_op_id, is_request.val(), payload_length.val());
#endif
    }

    static void printFullRow(const Tuple &t, Stats *v) {
	cout << format("%08x %10d %s %12s %8s %6lld %8.2f\n")
	    % host(t) % seconds(t) % isSendStr(t) % operationStr(t) 
	    % isRequestStr(t) % v->countll() % v->mean();
    }	
    
    static void printPartialRow(const Tuple &t, const bitset<5> &used, 
				Stats *v) {
	cout << format("%8s %10s %4s %12s %8s %6lld %8.2f\n")
	    % host(t,used) % seconds(t,used) % isSendStr(t,used) 
	    % operationStr(t,used) % isRequestStr(t,used) 
	    % v->countll() % v->mean();
    }

    virtual void printResult() {
	printf("Begin-%s\n",__PRETTY_FUNCTION__);
	cout << "host     time        dir          op    op-dir   count mean-payload\n";
	cube.cube();
	cube.printBase(boost::bind(&HostInfo::printFullRow, _1, _2));
	cube.printCube(boost::bind(&HostInfo::printPartialRow, _1, _2, _3));
	
#if 0
	uint32_t min_first_time = numeric_limits<uint32_t>::max();
	for(HashMap<uint32_t, PerHostData>::iterator i = host_to_data.begin();
	    i != host_to_data.end(); ++i) {
	    min_first_time = min(min_first_time, i->second.first_time_seconds);
	}
	if (min_first_time < numeric_limits<uint32_t>::max()) {
	    TotalTime total_time(min_first_time, group_seconds);
	    for(HashMap<uint32_t, PerHostData>::iterator i 
		    = host_to_data.begin();
		i != host_to_data.end(); ++i) {
		i->second.print(i->first, group_seconds, total_time);
	    }
	    total_time.print();
	}
#endif
	printf("End-%s\n",__PRETTY_FUNCTION__);
    }

    static Stats *makeStats() {
	return new Stats();
    }

    Int64TimeField packet_at;
    Int32Field payload_length;
    ByteField op_id, nfs_version;
    Int32Field source_ip, dest_ip;
    BoolField is_request;
    uint32_t group_seconds;

    //    enum KeyEnts { Host = 0, Seconds, IsSend, Operation, IsRequest, MaxEnts };
    // [int32 host, int32 seconds, bool is_send, byte op, bool is_request]
    //    StatsCube::CubeKey cube_key;
    StatsCube<Tuple> cube;
};

RowAnalysisModule *
NFSDSAnalysisMod::newHostInfo(DataSeriesModule &prev, char *arg)
{
    return new HostInfo(prev, arg);
}

