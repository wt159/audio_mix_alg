CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic
LDLIBS   = -lm

BIN     = audio_mix
BIN_DBG = audio_mix_debug

SRCS = audio_mix.c src/mix_algos.c src/limiter.c src/comp_soft.c src/truepeak.c
OBJS = $(SRCS:.c=.o)

.PHONY: all debug clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

debug: CFLAGS := -O0 -g -std=c99 -Wall -Wextra -pedantic -fsanitize=address,undefined
debug: $(BIN_DBG)

$(BIN_DBG): $(SRCS:.c=.dbg.o)
	$(CC) $(CFLAGS) -o $@ $(SRCS:.c=.dbg.o) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.dbg.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS) $(SRCS:.c=.dbg.o): src/mix_algos.h src/limiter.h src/comp_soft.h src/truepeak.h

clean:
	$(RM) $(BIN) $(BIN_DBG) $(OBJS) $(SRCS:.c=.dbg.o)
