CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c17 -Iinclude
LDFLAGS = -pthread

SRC     = src/main.c src/client.c src/room.c src/utils.c src/game.c src/config.c src/log.c
OBJ     = $(SRC:.c=.o)
BIN     = build/server

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p $(dir $(BIN))
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(BIN) 10000

clean:
	rm -f $(OBJ) $(BIN)
