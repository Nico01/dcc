CC = clang
CFLAGS += -Wall -g
LDFLAGS += `pkg-config --libs ncurses`

SOURCES  := $(wildcard *.c)
OBJECTS  := $(SOURCES:.c=.o)

all: dcc

dcc: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -f $(OBJECTS) dcc
