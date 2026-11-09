CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS := -I. -Iinclude
PA3_SOURCES := $(filter-out framework/main.cc,$(wildcard *.cc framework/*.cc \
    design/*.cc execution_unit/*.cc predictors/*.cc cache/*.cc))
PA3_SOURCES := $(filter-out statistics.cc,$(PA3_SOURCES))
PA4_SOURCES := $(filter-out framework/pa4/main.cc framework/pa4/noc_main.cc,\
    $(wildcard framework/pa4/*.cc noc/*.cc coherence/*.cc memory/*.cc) statistics.cc)
CPU_OBJECTS := $(addprefix .build/,$(PA3_SOURCES:.cc=.o))
SYSTEM_OBJECTS := $(addprefix .build/,$(PA4_SOURCES:.cc=.o))
all: pa4 pa4-noc
pa4: $(CPU_OBJECTS) $(SYSTEM_OBJECTS) .build/framework/pa4/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@
pa4-noc: $(SYSTEM_OBJECTS) .build/framework/pa4/noc_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@
.build/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
-include $(CPU_OBJECTS:.o=.d) $(SYSTEM_OBJECTS:.o=.d) \
    .build/framework/pa4/main.d .build/framework/pa4/noc_main.d
framework-test:
	python3 tests/framework.py
test: all
	python3 tests/run_tests.py
clean:
	rm -rf .build pa4 pa4-noc
.PHONY: all test clean framework-test
