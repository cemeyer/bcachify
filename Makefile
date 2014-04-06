CFLAGS ?= -g -pipe -Werror -Os
CFLAGS += -Wall -Wextra -std=gnu99

PROG = bcachify
SRCS = bcachify.c

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(PROG)
