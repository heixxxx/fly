FAKE_ALL: all

BINTARGET =	defrw

HEADERS =

PUBLIC_HDRS = 

LDLIBS = ../lib/libdef.a

BINSRCS =	\
			defrw.cpp

# Performance test binary with empty callbacks
PERF_TARGET = defrw_perf

include ../template.mk

# Additional target for performance testing
perf: $(PERF_TARGET)

$(PERF_TARGET): $(PERF_TARGET).cpp ../lib/libdef.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) -O2 -I../include -o ../bin/$@ $< $(LDLIBS) $(PROFLIB)

# Performance test with profiling support
perf_prof: $(PERF_TARGET).cpp ../lib/libdef.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) -O2 -I../include -o ../bin/$(PERF_TARGET)_prof $< $(LDLIBS) -ltcmalloc_and_profiler
