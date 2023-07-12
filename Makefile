CC ?= gcc

yesboard: yesboard.c
	$(CC) yesboard.c -lX11 -lXi -Wall -Wextra -Wpedantic -o yesboard
