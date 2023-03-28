TARGET_EXEC	:= perg

# Local directories
BUILD_DIR	:= ./build
SRC_DIR	:= ./src
INC_DIRS	:= $(shell find $(SRC_DIR) -type d)

# Source and object file names
SRCS	:= $(shell find $(SRC_DIR) -name '*.c')
OBJS	:= $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Installation directories
PREFIX	?= /usr/local
BINDIR	?= $(PREFIX)/bin
MANDIR	?= $(PREFIX)/share/man

CC	= gcc
CFLAGS	= -Wall -Werror -O3 -std=gnu89 -pthread

$(BUILD_DIR)/$(TARGET_EXEC) : $(OBJS)
	$(CC)  $(CFLAGS)  -o $@  $(OBJS)

$(BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC)  $(CFLAGS)  -c $<  -o $@

.PHONY: clean debug install uninstall

install : $(BUILD_DIR)/$(TARGET_EXEC)
	mkdir -p $(DESTDIR)$(BINDIR)
	install -m755 $(BUILD_DIR)/$(TARGET_EXEC) $(DESTDIR)$(BINDIR)/
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	# install manpage to $(DESTDIR)$(MANDIR)/man1/perg.1

uninstall :
	rm -f $(BINDIR)/$(TARGET_EXEC)
	rm -f $(MANDIR)/man1/perg.1

debug : clean
	CFLAGS += -Og -g
	$(TARGET_EXEC)

clean :
	rm -rf $(BUILD_DIR)
