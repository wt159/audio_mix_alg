CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -pedantic
LDLIBS   = -lm

BIN     = audio_mix
BIN_DBG = audio_mix_debug

.PHONY: all debug clean

all: $(BIN)

$(BIN): audio_mix.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

debug: CFLAGS := -O0 -g -std=c99 -Wall -Wextra -pedantic -fsanitize=address,undefined
debug: $(BIN_DBG)

$(BIN_DBG): audio_mix.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	$(RM) $(BIN) $(BIN_DBG)
