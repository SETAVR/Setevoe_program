CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

all: lab1

lab1: lab1.c

test: lab1
	python3 tests.py ./lab1

clean:
	rm -f lab1 lab1.exe

.PHONY: all test clean
