CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2

wish: wish.c
	$(CC) $(CFLAGS) -o wish wish.c

clean:
	rm -f wish

.PHONY: clean
