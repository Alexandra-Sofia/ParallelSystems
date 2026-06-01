CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp -pthread
LDFLAGS = -fopenmp -pthread -lm

TARGETS = ex1 ex2 ex3 ex4 ex5 ex6

.PHONY: all clean

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)