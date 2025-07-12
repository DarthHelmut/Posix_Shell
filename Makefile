# Makefile for SLSHELL

TARGET = SLSHELL
SRC = SLSHELL.c
CC = gcc
CFLAGS = -Wall -Wextra -O2 -s -lreadline

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

strip: SLSHELL
	strip SLSHELL

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/bin/$(TARGET)

uninstall:
	rm -f /usr/bin/$(TARGET)

clean:
	rm -f $(TARGET)

