CFLAGS ?= -g -pipe -Werror -Os
CFLAGS += -Wall -Wextra -std=gnu99

bcachify: bcachify.c
	$(CC) $(CFLAGS) -o $@ $<
