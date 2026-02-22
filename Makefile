PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

CC = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 $(shell pkg-config --cflags xft)
LDFLAGS = -lX11 $(shell pkg-config --libs xft)

BIN = opendwm
SRC = opendwm.c

all: $(BIN)


config.h: config.def.h
	@test -f $@ || cp $< $@

$(BIN): $(SRC) config.h
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

install: $(BIN)
	mkdir -p $(BINDIR)
	install -m 755 $(BIN) $(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)

.PHONY: all install clean
