BIN      := frame23a
PREFIX   ?= /usr/local
BINDIR   := $(DESTDIR)$(PREFIX)/bin

CC       ?= cc

# CFLAGS is the tunable part, so `make CFLAGS="-O2 -Werror"` works without
# dropping the flags the build cannot compile without. A command-line
# assignment overrides `+=`, which is why these are kept separate.
CFLAGS   ?= -O2 -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes

REQUIRED_CFLAGS := -std=c11 -D_GNU_SOURCE -Iinclude
ALL_CFLAGS       = $(REQUIRED_CFLAGS) $(CFLAGS)

LDFLAGS  ?=
LDLIBS   := -lm

SRC      := main.c $(wildcard libs/frame23a/*.c)
OBJ      := $(SRC:.c=.o)
DEP      := $(OBJ:.o=.d)

.PHONY: all clean install uninstall check-deps

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

install: $(BIN)
	install -d $(BINDIR)
	install -m 0755 $(BIN) $(BINDIR)/$(BIN)

uninstall:
	rm -f $(BINDIR)/$(BIN)

# Reports what is missing and how to install it, rather than failing opaquely
# at runtime on a machine without ffmpeg.
check-deps: $(BIN)
	@./$(BIN) check-deps
