all := libjoint test test_extended joint_axis_store_test joint_axis_roundtrip_test

LDLIBS-libjoint := -lqmap -lqsys
LDLIBS-test := -lqmap -lqsys -ljoint
LDLIBS-test_extended := -lqmap -lqsys -ljoint
LDLIBS-joint_axis_store_test := -lqmap -lqsys -ljoint
LDLIBS-joint_axis_roundtrip_test := -lqmap -lqsys -ljoint

CFLAGS += -g
CFLAGS += -O3 -mpopcnt -mavx2 -mfma
CFLAGS += -I/home/quirinpa/site/external/libqmap/include

include ../mk/include.mk

test: all
	./test.sh

bench: all
	LD_LIBRARY_PATH=lib:$(LD_LIBRARY_PATH) ./bin/test_extended 2>&1 | grep -E 'µs|PASS|FAIL|ALL|==='
