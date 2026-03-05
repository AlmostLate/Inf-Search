CC      = gcc
CXX     = g++
CFLAGS  = -Wall -Wextra -O2
CXXFLAGS = -Wall -Wextra -O2 -std=c++11

all: tokenizer indexer search_cli

tokenizer: tokenizer.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

indexer: indexer.cpp growbuf.h robinmap.h cstr.h stemmer.h
	$(CC) $(CFLAGS) -o $@ indexer.cpp -lm

search_cli: search_cli.cpp growbuf.h cstr.h stemmer.h
	$(CC) $(CFLAGS) -o $@ search_cli.cpp -lm

clean:
	rm -f tokenizer indexer search_cli

.PHONY: all clean
