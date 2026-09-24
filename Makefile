all = libjoint test test_extended joint_axis_store_test ${all-extra-${SYS}}
all-extra-Unix := joint_axis_roundtrip_test

LDLIBS-libjoint := -lcorm -lqsys
LDLIBS-test := -lcorm -lqsys -ljoint
LDLIBS-test_extended := -lcorm -lqsys -ljoint
LDLIBS-joint_axis_store_test := -lcorm -lqsys -ljoint
LDLIBS-joint_axis_roundtrip_test := -lcorm -lqsys -ljoint

CFLAGS += -g
CFLAGS += -O3
CFLAGS += -I/home/quirinpa/site/external/libcorm/include

CFLAGS-x86_64 := -mpopcnt -mavx2 -mfma
CFLAGS-amd64 := -mpopcnt -mavx2 -mfma

include ../mk/include.mk

test: all
	./test.sh

bench: all
	LD_LIBRARY_PATH=lib:$(LD_LIBRARY_PATH) ./bin/test_extended 2>&1 | grep -E 'µs|PASS|FAIL|ALL|==='
