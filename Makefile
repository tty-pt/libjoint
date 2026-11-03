all = libjoint test test_extended joint_axis_store_test ${all-extra-${SYS}}
all-extra-Unix := joint_axis_roundtrip_test

LDLIBS-libjoint := -lcorm -lstele
LDLIBS-test := -lcorm -lstele -ljoint
LDLIBS-test_extended := -lcorm -lstele -ljoint
LDLIBS-joint_axis_store_test := -lcorm -lstele -ljoint
LDLIBS-joint_axis_roundtrip_test := -lcorm -lstele -ljoint

CFLAGS += -g
CFLAGS += -O3

CFLAGS-x86_64 := -mpopcnt -mavx2 -mfma
CFLAGS-amd64 := -mpopcnt -mavx2 -mfma

INSTALL_BIN :=

WASM_SRCS := npm/wasm/joint_wasm.c
WASM_LDFLAGS += -Wl,--import-memory

include ../mk/include.mk

test: all
	./test.sh

bench: all
	LD_LIBRARY_PATH=lib:$(LD_LIBRARY_PATH) ./bin/test_extended 2>&1 | grep -E 'µs|PASS|FAIL|ALL|==='

# Optional Node.js & WebAssembly bindings
NPM ?= npm

npm-build:
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) run build); \
	else \
		echo "Skipping npm build — $(NPM) not found"; \
	fi

npm-test: npm-build
	@if command -v $(NPM) >/dev/null 2>&1; then \
		(cd npm && $(NPM) test); \
	fi

