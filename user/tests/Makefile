HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -I../calc

.PHONY: all clean

all: calc_test

calc_test: calc_test.c ../calc/calc.c ../calc/calc.h
	$(HOST_CC) $(HOST_CFLAGS) -o $@ calc_test.c ../calc/calc.c
	./calc_test

clean:
	rm -f calc_test
