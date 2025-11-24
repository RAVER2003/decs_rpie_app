CXX = g++
CXXFLAGS = -std=c++17 -O2 -pthread -Wall -Wextra
LIBS = -lmariadb

SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

all: kv_server

kv_server: main.o
	$(CXX) $(CXXFLAGS) -o kv_server main.o $(LIBS)

main.o: main.cpp util.hpp config.hpp conn_table.hpp worker_pool.hpp db.hpp lru_cache.hpp job.hpp conn.hpp
	$(CXX) $(CXXFLAGS) -c main.cpp

clean:
	rm -f *.o kv_server
