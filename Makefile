CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wpedantic -std=c11 -fopenmp
LDFLAGS = -lpthread -lm
SRCDIR  = src
BINDIR  = bin
TARGETS = $(addprefix $(BINDIR)/,ex1 ex2 ex3 ex4 ex5 ex6)

.PHONY: all clean

all: $(BINDIR) $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/ex1: $(SRCDIR)/ex1.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/ex2: $(SRCDIR)/ex2.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/ex3: $(SRCDIR)/ex3.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/ex4: $(SRCDIR)/ex4.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/ex5: $(SRCDIR)/ex5.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BINDIR)/ex6: $(SRCDIR)/ex6.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BINDIR)
