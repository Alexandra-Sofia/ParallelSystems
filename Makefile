CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp
LDFLAGS = -lpthread -lm

TARGETS = ex1 ex2 ex3 ex4 ex5 ex6

.PHONY: all clean

all: $(TARGETS)

ex1: ex1.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ex2: ex2.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ex3: ex3.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ex4: ex4.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ex5: ex5.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ex6: ex6.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
