#!/bin/sh -e

# Set library path for test binary
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# Clean up any existing test databases
rm -f *.corm *.corm.* 2>/dev/null || true

# Run the test suite and compare output
./bin/test | diff expects.txt -

echo "Core tests passed!"

# Run extended test suite (stress tests, performance benchmarks)
./bin/test_extended

echo "Extended tests passed!"

# Run the 2A-3 adapter/erase contract test
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/joint_axis_store_test

# Run the 2A-5 file-backed cross-process round-trip test
LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH ./bin/joint_axis_roundtrip_test

echo "All tests passed!"
