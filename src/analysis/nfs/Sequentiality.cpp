#include <Lintel/HashMap.hpp>
#include <Lintel/LintelLog.hpp>
#include <Lintel/RotatingHashMap.hpp>
#include <Lintel/StatsQuantile.hpp>

#include <DataSeries/ExtentField.hpp>
#include <DataSeries/RowAnalysisModule.hpp>

#include <analysis/nfs/common.hpp>
#include <analysis/nfs/Tuples.hpp>

using namespace std;
using boost::format;
using dataseries::TFixedField;

class Sequentiality : public RowAnalysisModule {
public:
    Sequentiality(DataSeriesModule &source, const string &arg_str)
        : RowAnalysisModule(source),
          request_at(series, "request_at"),
          reply_at(series, "reply_at"),
          server(series, "server"),
          client(series, "client"),
          filehandle(series, "filehandle"),
          is_read(series, "is_read"),
          file_size(series, "file_size", Field::flag_nullable),
          modify_time(series, "modify_time", Field::flag_nullable),
          offset(series, "offset"),
          bytes(series, "bytes"),
	  last_rotate_time_raw(numeric_limits<int64_t>::min()),
	  reset_interval_raw(0),
	  skip_distribution(0.001, static_cast<uint64_t>(1.0e10)),
	  last_reported_memory_usage(0)
    { 
	ignore_client = false;
	ignore_server = false;
	vector<string> args = split(arg_str, ",");
	for(unsigned i = 0; i < args.size(); ++i) {
	    if (args[i] == "ignore_client") {
		ignore_client = true;
	    } else if (args[i] == "ignore_server") {
		ignore_server = true;
	    } else if (args[i].empty()) {
		// ignore
	    } else {
		FATAL_ERROR("?");
	    }
	}
	if (LintelLog::wouldDebug("memory_usage")) {
	    last_reported_memory_usage = 1;
	    reportMemoryUsage();
	} 
    }

    virtual ~Sequentiality() { }

    struct Operation {
	int64_t request_at, reply_at;
	int64_t start_offset;
	uint32_t len;

	Operation(int64_t a, int64_t b, int64_t c, uint32_t d) 
	    : request_at(a), reply_at(b), start_offset(c), len(d)
	{ }
    };

    struct FHState {
	int64_t last_end_offset;
	int64_t last_operation;
	uint32_t write_count, read_count;
	uint32_t random_count, sequential_count;
	uint32_t bytes_read;

	//	Deque<Operation> ops;
	FHState() { reset(); }

	void reset() {
	    last_end_offset = numeric_limits<int64_t>::min();
	    last_operation = numeric_limits<int64_t>::min();
	    write_count = 0; 
	    read_count = 0; 
	    random_count = 0;
	    sequential_count = 0;
	    bytes_read = 0;
	}
    };

    typedef boost::tuple<uint64_t, int32_t, int32_t> Key;

    virtual void prepareForProcessing() {
	reset_interval_raw = request_at.secNanoToRaw(30, 0);
    }

    void completeOpRun(FHState &state) {
	state.reset();
    }

    void reportMemoryUsage() {
	size_t a = states.memoryUsage();
	size_t b = skip_distribution.memoryUsage();
	LintelLogDebug("memory_usage",
		       format("# Memory-Usage: Sequentiality %d = %d + %d") % (a + b) % a % b);
	last_reported_memory_usage = a + b;
    }
	
    virtual void newExtentHook(const Extent &e) {
	if (last_reported_memory_usage > 0) {
	    size_t memory_usage = states.memoryUsage() + skip_distribution.memoryUsage();
	    if (memory_usage > (last_reported_memory_usage  + 4 * 1024 * 1024)) {
		reportMemoryUsage();
	    }
	}
    }

    virtual void processRow() {
	if (reply_at.valRaw() > last_rotate_time_raw + reset_interval_raw) {
	    // TODO: verify they are clean.
	    states.rotate();
	    last_rotate_time_raw = reply_at.valRaw();
	}
	FHState &state = states[Key(md5FileHash(filehandle),
				    ignore_server ? 0 : server.val(), 
				    ignore_client ? 0 : client.val())];

#if 0
	state.ops.push_back(Operation(request_at.val(), reply_at.val(),
				      offset.val(), len.val());
#endif
	if (state.last_operation + reset_interval_raw < reply_at.valRaw()
	    && (state.write_count + state.read_count) > 0) {
	    completeOpRun(state);
	}
	if (state.last_end_offset == offset.val()) {
	    ++state.sequential_count;
	} else {
	    ++state.random_count;
	}

	if (is_read.val()) {
	    ++state.read_count;
	} else {
	    ++state.write_count;
	}

	if (state.last_end_offset != numeric_limits<int64_t>::min()) {
	    int64_t skip = offset.val() - state.last_end_offset;
	    skip_distribution.add(skip);
	}
	state.last_end_offset = offset.val() + bytes.val();
	state.last_operation = reply_at.valRaw();
    }

    virtual void printResult() {
	cout << format("Begin-%s\n") % __PRETTY_FUNCTION__;
	reportMemoryUsage();
	cout << "Analysis configuration: ";
	if (ignore_client) {
	    cout << "ignore client, ";
	} else {
	    cout << "distinct client, ";
	}
	if (ignore_server) {
	    cout << "ignore server\n";
	} else {
	    cout << "distinct server\n";
	}

	cout << "Skip distribution:\n";
	skip_distribution.printTextRanges(cout, 1000);

	cout << format("End-%s\n") % __PRETTY_FUNCTION__;
    }

private:
    Int64TimeField request_at;
    Int64TimeField reply_at;
    TFixedField<int32_t> server;
    TFixedField<int32_t> client;
    Variable32Field filehandle;
    BoolField is_read;
    Int64Field file_size;
    Int64Field modify_time;
    TFixedField<int64_t> offset;
    TFixedField<int32_t> bytes;

    RotatingHashMap<Key, FHState, TupleHash<Key> > states;
    int64_t last_rotate_time_raw;
    int64_t reset_interval_raw;

    bool ignore_client, ignore_server;
    StatsQuantile skip_distribution;

    size_t last_reported_memory_usage;
};

namespace NFSDSAnalysisMod {
    RowAnalysisModule *
    newSequentiality(DataSeriesModule &prev, const string &arg) {
	return new Sequentiality(prev, arg);
    }
}