CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
TARGET  = visualSSS
SRC     = visualSSS.c utils/utils.c shamir/shamir.c shamir/prng/prng.c shamir/bmp/bmp.c
OBJ     = $(SRC:.c=.o)
HEADERS = utils/utils.h shamir/shamir.h shamir/prng/prng.h shamir/bmp/bmp.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
