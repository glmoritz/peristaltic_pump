# Host-only closed-loop test of the control state machine (no Zephyr needed).
#   make -C firmware/tests/host

SRC_DIR := ../../src
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wdouble-promotion -I$(SRC_DIR)

test_control: test_control.c $(SRC_DIR)/control.c $(SRC_DIR)/control.h
	$(CC) $(CFLAGS) -Wno-double-promotion -o $@ test_control.c $(SRC_DIR)/control.c -lm

.PHONY: run clean
run: test_control
	./test_control

clean:
	rm -f test_control

.DEFAULT_GOAL := run
