CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
TARGET  = visualSSS
SRC     = visualSSS.c
 
.PHONY: all clean
 
all: $(TARGET)
 
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
 
clean:
	rm -f $(TARGET)
 