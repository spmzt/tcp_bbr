# -Werror
CFLAGS  = -g -Wall -Wextra
CC      = clang-18
RM		= rm

FILE ?= size_t

BUILD_DIR = bbr
SOURCES = $(FILE).c
OBJECTS = $(SOURCES:.c=.o)
TARGET  = $(FILE)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $^

.PHONY: clean build

clean:
	$(RM) -f build/$(TARGET) $(OBJECTS) core

build: clean $(TARGET)
