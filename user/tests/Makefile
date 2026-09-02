HOST_CC := gcc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -I../calc
CONTRACT_CFLAGS := $(HOST_CFLAGS) -Wno-pointer-to-int-cast

.PHONY: all clean launch_contract_test

all: calc_test launch_contract_test

calc_test: calc_test.c ../calc/calc.c ../calc/calc.h
	$(HOST_CC) $(HOST_CFLAGS) -o $@ calc_test.c ../calc/calc.c
	./calc_test

launch_contract_test: launch_contract_test.c ../calc/syscall.h
	$(MAKE) -C .. calc
	$(HOST_CC) $(CONTRACT_CFLAGS) -o $@ launch_contract_test.c
	./launch_contract_test ../calc/calc

clean:
	rm -f calc_test launch_contract_test
