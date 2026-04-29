CXX      := clang++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic
DOCTEST  := $(shell brew --prefix doctest)/include

.PHONY: all example test check clean

all: example test

example: example.cpp htpp.hpp
	$(CXX) $(CXXFLAGS) -O3 -o $@ $<

test: test.cpp htpp.hpp
	$(CXX) $(CXXFLAGS) -I$(DOCTEST) -o $@ $<

check: test
	./test

clean:
	rm -f example test
