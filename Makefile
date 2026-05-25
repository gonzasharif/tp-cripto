CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
TARGET  = visualSSS
SRC     = visualSSS.c utils/utils.c
OBJ     = $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c utils/utils.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
