CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags gtk+-3.0)
LDLIBS  = $(shell pkg-config --libs gtk+-3.0)
SRCS    = $(wildcard src/*.c)
OBJS    = $(SRCS:.c=.o)
BIN     = fm

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: clean
