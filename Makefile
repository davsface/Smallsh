CC = gcc
CFLAGS = -std=c99 -g
TARGET = smallsh

$(TARGET): smallsh.c
	$(CC) $(CFLAGS) -o $(TARGET) smallsh.c
