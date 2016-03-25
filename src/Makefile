CC = clang
CFLAGS += -Wall -Wno-switch-enum
LDFLAGS += -lncurses

SOURCES  := $(wildcard *.c)
OBJECTS  := $(SOURCES:.c=.o)

all: dcc

dcc: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -f $(OBJECTS) dcc
