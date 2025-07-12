# Makefile for shell_min

TARGET = SLSHELL
SRC = SLSHELL.c
CC = gcc
CFLAGS = -Wall -Wextra -O2 -s -lreadline

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

strip: shell_min
	strip shell_min

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET)

