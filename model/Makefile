# wren7 ARM7DI model.  Every tools/<name>.cpp -> ../build/model/<name>, linked with the model sources.
# Build artifacts live under wren7-rtl/build/ only (git-ignored).  Run the tools from wren7-rtl/model.
#   make            build everything
#   make test       vendor suite, data-sheet timing checks, kept qemu_diff batches (writes tests/*_summary.txt)
BUILD = ../build/model
CXXFLAGS = -O2 -g -Wall -Wextra -std=c++17 -fopenmp
SRCS = $(wildcard src/*.cpp)
HDRS = $(wildcard src/*.h tools/*.h)
OBJS = $(SRCS:src/%.cpp=$(BUILD)/obj/%.o)
TOOLS = $(basename $(notdir $(wildcard tools/*.cpp)))
.PHONY: all clean test $(TOOLS)
all: $(TOOLS)
$(TOOLS): %: $(BUILD)/%
$(BUILD)/obj/%.o: src/%.cpp $(HDRS)
	@mkdir -p $(BUILD)/obj
	g++ $(CXXFLAGS) -c -o $@ $<
$(BUILD)/%: tools/%.cpp $(OBJS) $(HDRS)
	@mkdir -p $(BUILD)
	g++ $(CXXFLAGS) -o $@ $< $(OBJS)
test: all
	$(BUILD)/vendor_tests | tee tests/vendor_summary.txt
	$(BUILD)/timing_check | tee tests/timing_summary.txt
	$(BUILD)/qemu_diff | tee tests/qemu_diff_summary.txt
clean:
	-rm -rf $(BUILD)
