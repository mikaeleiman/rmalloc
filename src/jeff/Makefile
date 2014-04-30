.PHONY: tests

all: compact.o listsort.o
all: CFLAGS += -O3 -march=core2

debugcode: compact.o listsort.o
debugcode: CFLAGS += -g -O0 -DDEBUG

debug: compact.o listsort.o
debug: CFLAGS += -g -O0

profile: compact.o listsort.o
profile: PROFILING=-pg -g3
#profile: CFLAGS += -O3 -march=core2

SOURCES=listsort.c compact.c
CFLAGS=

# We need to download the zip first
build/gtest.zip : | build
	curl -o build/gtest.zip -L http://googletest.googlecode.com/files/gtest-1.7.0.zip

# Then extract it in the correct place. We only care that the zip exists, not that it's recent
tests/gtest-1.7.0 : | build/gtest.zip
	unzip build/gtest.zip -d tests


#### begin googletest

GTEST_DIR = tests/gtest-1.7.0

# Flags passed to the preprocessor.
CPPFLAGS += -I$(GTEST_DIR)/include

# Flags passed to the C++ compiler.
CXXFLAGS += -g -Wall -Wextra

# All Google Test headers.  Usually you shouldn't change this
# definition.
GTEST_HEADERS = $(GTEST_DIR)/include/gtest/*.h \
				$(GTEST_DIR)/include/gtest/internal/*.h

# Usually you shouldn't tweak such internal variables, indicated by a
# trailing _.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

# Build gtest binaries
build/gtest-all.o : $(GTEST_DIR)/src/gtest-all.cc
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest-all.cc -o build/gtest-all.o

build/gtest_main.o : $(GTEST_DIR)/src/gtest_main.cc
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c $(GTEST_DIR)/src/gtest_main.cc -o build/gtest_main.o

#### end googletest


.c.o:
	gcc $(PROFILING) -c $(CFLAGS) $<

tests: test_*.cpp $(SOURCES)
	gcc -o $(CFLAGS) run_tests test_*.cpp $(SOURCES) -Itests/gtest-1.7.0/include -g -lstdc++ build/gtest_main.o build/gtest-all.o -lpthread

compact: $(SOURCES)
	gcc -o compact $(SOURCES)

clean:
	rm -rf *.o run_tests compact
