CXX ?= clang++
CXXFLAGS ?= -O2 -std=c++11
OUTPUT ?= /tmp/bezierpath-native-benchmark
TEST_OUTPUT ?= /tmp/bezierpath-native-tests
LEGACY_OUTPUT ?= /tmp/bezierpath-native-benchmark-legacy
LEGACY_LIBRARY ?= ../build/arm64-osx/libBezierPath_6.a
LUA ?= luajit

ROOT := ..
INCLUDES := -Istubs -I$(ROOT)/bezierpath/include
SOURCES := \
	native_benchmark.cpp \
	$(ROOT)/bezierpath/src/utils.cpp \
	$(ROOT)/bezierpath/src/fill.cpp \
	$(ROOT)/bezierpath/src/shadow.cpp \
	$(ROOT)/bezierpath/src/stroke.cpp

TEST_SOURCES := \
	native_tests.cpp \
	$(ROOT)/bezierpath/src/path_registry.cpp \
	$(ROOT)/bezierpath/src/utils.cpp \
	$(ROOT)/bezierpath/src/fill.cpp \
	$(ROOT)/bezierpath/src/shadow.cpp \
	$(ROOT)/bezierpath/src/stroke.cpp

.PHONY: run test lua-test legacy
run:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $(OUTPUT)
	$(OUTPUT)

test:
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SOURCES) -o $(TEST_OUTPUT)
	$(TEST_OUTPUT)

lua-test:
	$(LUA) lua_api_test.lua

legacy:
	$(CXX) $(CXXFLAGS) -DLEGACY_RASTER -Istubs native_benchmark.cpp $(LEGACY_LIBRARY) -o $(LEGACY_OUTPUT)
	$(LEGACY_OUTPUT)
