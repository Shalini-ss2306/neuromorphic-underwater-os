CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pthread
TARGET = underwater_os

all: $(TARGET)

$(TARGET): underwater_os.c
	$(CC) $(CFLAGS) underwater_os.c -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) underwater_os_log.csv

.PHONY: all run clean
