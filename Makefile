CC = gcc
CFLAGS = -c -Wall -Werror -Isrc

SOURCE_DIR = src
BUILD_DIR = bin

UTIL_BUILD_DIR = $(BUILD_DIR)/util
UTIL_SOURCE_DIR = $(SOURCE_DIR)/util
UTIL_SOURCES = $(wildcard $(UTIL_SOURCE_DIR)/*.c)
UTIL_OBJECTS = $(subst $(SOURCE_DIR), $(BUILD_DIR), $(patsubst %.c, %.o, $(UTIL_SOURCES)))

VSWITCH_BUILD_DIR = $(BUILD_DIR)/vswitch
VSWITCH_SOURCE_DIR = $(SOURCE_DIR)/vswitch
VSWITCH_TARGET = $(VSWITCH_BUILD_DIR)/vswitch
VSWITCH_SOURCES = $(wildcard $(VSWITCH_SOURCE_DIR)/*.c)
VSWITCH_OBJECTS = $(subst $(SOURCE_DIR), $(BUILD_DIR), $(patsubst %.c, %.o, $(VSWITCH_SOURCES)))

VWIRE_BUILD_DIR = $(BUILD_DIR)/vwire
VWIRE_SOURCE_DIR = $(SOURCE_DIR)/vwire
VWIRE_TARGET = $(VWIRE_BUILD_DIR)/vwire
VWIRE_SOURCES = $(wildcard $(VWIRE_SOURCE_DIR)/*.c)
VWIRE_OBJECTS = $(subst $(SOURCE_DIR), $(BUILD_DIR), $(patsubst %.c, %.o, $(VWIRE_SOURCES)))

VSWSH_BUILD_DIR = $(BUILD_DIR)/vswsh
VSWSH_SOURCE_DIR = $(SOURCE_DIR)/vswsh
VSWSH_TARGET = $(VSWSH_BUILD_DIR)/vswsh
VSWSH_SOURCES = $(wildcard $(VSWSH_SOURCE_DIR)/*.c)
VSWSH_OBJECTS = $(subst $(SOURCE_DIR), $(BUILD_DIR), $(patsubst %.c, %.o, $(VSWSH_SOURCES)))

all: vswitch vwire vswsh

vswitch: $(VSWITCH_OBJECTS) $(UTIL_OBJECTS)
	$(CC) -o $(VSWITCH_TARGET) $^
	@echo "===== BUILT VSWITCH ====="

vwire: $(VWIRE_OBJECTS) $(UTIL_OBJECTS)
	$(CC) -o $(VWIRE_TARGET) $^
	@echo "===== BUILT vwire ====="

vswsh: $(VSWSH_OBJECTS)
	$(CC) -o $(VSWSH_TARGET) $^
	@echo "===== BUILT VSWSH ====="

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR):
	@echo "===== SETTING UP BUILD DIRECTOERY ====="
	mkdir -p $(BUILD_DIR)
	mkdir -p $(UTIL_BUILD_DIR)
	mkdir -p $(VSWITCH_BUILD_DIR)
	mkdir -p $(VWIRE_BUILD_DIR)
	mkdir -p $(VSWSH_BUILD_DIR)
	@echo "======================================="

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean