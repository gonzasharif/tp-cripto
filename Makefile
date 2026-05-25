CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
TARGET  = visualSSS
SRC     = visualSSS.c utils.c
OBJ     = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c utils.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
