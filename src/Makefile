CXX      = g++
CC       = gcc
CXXFLAGS = -O2 -std=c++17 -Wall -fopenmp
CFLAGS   = -O2 -Wall
LDFLAGS  = -fopenmp
LDLIBS   = -lm

MAIN_SRCS = main.cpp DS_timer.cpp
MAIN_OBJS = $(MAIN_SRCS:.cpp=.o)

GEN_SRC  = generator.c
GEN_OBJ  = $(GEN_SRC:.c=.o)

TARGETS  = main.exe generator.exe

.PHONY: all clean

all: $(TARGETS)

main.exe: $(MAIN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

generator.exe: $(GEN_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(MAIN_OBJS) $(GEN_OBJ) $(TARGETS)
