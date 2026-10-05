CXX = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fno-fast-math
CPPFLAGS = -Iinclude
PYTHON = python3

.PHONY: all test clean
all: build/ventus-machine-timing build/ventus-benchmark

build:
	mkdir -p build

build/ventus-machine-timing: src/main.cpp include/execution_system.hpp include/memory_system.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

build/ventus-benchmark: src/benchmark.cpp include/execution_system.hpp include/memory_system.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

test: all
	$(PYTHON) tools/validate.py

clean:
	rm -rf build
