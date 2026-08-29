# 16.16 fixed point -- built and tested twice, on purpose.
#
#   make test       gcc, this PC:            int is 32 bits
#   make test6502   cc65 + sim65, a 6502:    int is 16 bits
#   make check      both, which is the only combination that means anything
#   make compare    the original next to the corrected version, against double
#
# The original bugs all passed a host build and all failed on the target, so a
# suite that only runs one of these proves nothing about the other.

CC      ?= gcc
CFLAGS  ?= -O1 -std=c89 -Wall -Wextra
CL65    ?= cl65
SIM65   ?= sim65

.PHONY: all check test test6502 compare clean

all: check

check: test test6502
	@echo
	@echo "PASS on both a 32-bit int host and a 16-bit int 6502"

test: test_host
	@echo "== gcc, this machine =="
	@./test_host

test6502: test6502.prg
	@echo "== cc65 + sim65, a real 6502 =="
	@$(SIM65) test6502.prg

test_host: test_fixed.c fixed_math.c fixed_math.h
	$(CC) $(CFLAGS) -o $@ test_fixed.c fixed_math.c

# cc65 warns "Constant is long" on every table entry above 32767, which is
# correct and expected for a uint16_t table -- filtered so real warnings show.
test6502.prg: test_fixed.c fixed_math.c fixed_math.h
	@$(CL65) -t sim6502 -O -o $@ test_fixed.c fixed_math.c 2>&1 \
	  | grep -v "Constant is long" || true
	@test -f $@

compare: compare_host
	@./compare_host

compare_host: compare.c fixed_math.c fixed_math.h
	$(CC) -O1 -std=gnu99 -o $@ compare.c fixed_math.c -lm

clean:
	rm -f test_host test6502.prg compare_host *.o
