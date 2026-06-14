CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -Isrc

# Detect OS — set executable name and delete command accordingly
ifeq ($(OS), Windows_NT)
    TARGET = mnemosyne.exe
    DEL    = del /Q
else
    TARGET = mnemosyne
    DEL    = rm -f
endif

SRCS = src/main.c \
       src/help.c \
       src/command_handler.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Debug build with sanitizers — Linux/macOS only (not supported by MinGW on Windows)
sanitize: $(SRCS)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer $(SRCS) -o $(TARGET)

clean:
	$(DEL) $(TARGET)

.PHONY: clean sanitize
