TARGET_EXEC	:= perg

BUILD_DIR	:= ./build
SRC_DIR	:= ./src
INC_DIRS := $(shell find $(SRC_DIR) -type d)

SRCS	:= $(shell find $(SRC_DIR) -name '*.c')
OBJS	:= $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CC	= gcc
CFLAGS	= -Wall -Werror -O3 -std=gnu89 -pthread

$(BUILD_DIR)/$(TARGET_EXEC) : $(OBJS)
	$(CC)  $(CFLAGS)  -o $@  $(OBJS)

$(BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC)  $(CFLAGS)  -c $<  -o $@

.PHONY: clean debug

install : $(BUILD_DIR)/$(TARGET_EXEC)
	cp $(BUILD_DIR)/$(TARGET_EXEC) /usr/local/bin

uninstall :
	rm -f /usr/local/bin/$(TARGET_EXEC)

debug : clean
	CFLAGS += -Og -g
	$(TARGET_EXEC)

clean :
	rm -rf $(BUILD_DIR)
