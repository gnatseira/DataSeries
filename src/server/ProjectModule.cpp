#include "DSSModule.hpp"

class ProjectModule : public OutputSeriesModule {
public:
    ProjectModule(DataSeriesModule &source, const vector<string> &keep_columns)
        : source(source), keep_columns(keep_columns), copier(input_series, output_series)
    { }

    virtual ~ProjectModule() { }

    Extent *returnOutputSeries() {
        Extent *ret = output_series.getExtent();
        output_series.clearExtent();
        return ret;
    }

    void firstExtent(Extent &in) {
        const ExtentType &t(in.getType());
        input_series.setType(t);

        string output_xml(str(format("<ExtentType name=\"project (%s)\" namespace=\"%s\""
                                     " version=\"%d.%d\">\n") % t.getName() % t.getNamespace()
                              % t.majorVersion() % t.minorVersion()));
        HashUnique<string> kc;
        BOOST_FOREACH(const string &c, keep_columns) {
            kc.add(c);
        }
        for (uint32_t i = 0; i < t.getNFields(); ++i) {
            const string &field_name(t.getFieldName(i));
            if (kc.exists(field_name)) {
                output_xml.append(t.xmlFieldDesc(field_name));
            }
        }
        output_xml.append("</ExtentType>\n");
        ExtentTypeLibrary lib;
        const ExtentType &output_type(lib.registerTypeR(output_xml));
            
        output_series.setType(output_type);

        copier.prep();
    }

    virtual Extent *getExtent() {
        while (true) {
            Extent *in = source.getExtent();
            if (in == NULL) {
                return returnOutputSeries();
            }
            if (input_series.getType() == NULL) {
                firstExtent(*in);
            }

            if (output_series.getExtent() == NULL) {
                output_series.setExtent(new Extent(*output_series.getType()));
            }
        
            for (input_series.setExtent(in); input_series.more(); input_series.next()) {
                output_series.newRecord();
                copier.copyRecord();
            }
            if (output_series.getExtent()->size() > 96*1024) {
                return returnOutputSeries();
            }
        }
    }

    DataSeriesModule &source;
    ExtentSeries input_series;
    vector<string> keep_columns;
    ExtentRecordCopy copier;
};

OutputSeriesModule::OSMPtr 
dataseries::makeProjectModule(DataSeriesModule &source, const vector<string> &keep_columns) {
    return OutputSeriesModule::OSMPtr(new ProjectModule(source, keep_columns));
}

