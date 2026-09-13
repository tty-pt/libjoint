# libjoint - Interval Tree Library

A high-performance C library for storing and querying time-based intervals. Built on top of [qmap](https://github.com/tty-pt/qmap) for efficient sorted storage and iteration.

## What is libjoint?

libjoint allows you to:

- **Store intervals**: Record start/stop times for any entity (users, processes, events)
- **Query overlaps**: Find all intervals that intersect a given time range
- **Split intervals**: Decompose overlapping intervals into non-overlapping segments
- **Persist data**: Save intervals to disk and reload them later

## Features

- **High capacity**: Store up to 65,536 intervals per database
- **High overlap support**: Handle up to 4,096 concurrent overlapping entities
- **Fast queries**: Microsecond-level query performance
- **File persistence**: Built on qmap for reliable disk storage
- **Clean API**: Simple C interface with error handling
- **Zero-copy iteration**: Efficient traversal of query results

## Quick Example

```c
#include <ttypt/joint.h>

// Create database (NULL = memory only, "data.qmap" = persisted)
unsigned jd = joint_init(NULL);

// Record that entity 1 was active from time 1000 to 2000
joint_start(jd, 1000, 1);
joint_stop(jd, 2000, 1);

// Query: which entities were active between 1200 and 1800?
joint_cur_t cur = joint_iter(jd, 1200, 1800);
time_t min, max;
unsigned count, who;

while (joint_next(&min, &max, &count, &who, &cur)) {
    printf("Entity %u active [%ld, %ld)\n", who, min, max);
}
```

## Installation

```bash
# Clone and build
git clone https://github.com/tty-pt/libjoint.git
cd libjoint
make all

# Run tests
./test.sh

# Or run manually
LD_LIBRARY_PATH=./lib ./bin/test
```

## Dependencies

- **qmap** - Sorted key-value store with persistence
- **qsys** - System utilities library
- **libc** - Standard C library (already on your system)

These are automatically built and linked when you run `make`.

## Documentation

| Document | Description |
|----------|-------------|
| [QUICK_REFERENCE.md](./QUICK_REFERENCE.md) | API overview with code examples |
| [CHANGELOG.md](./CHANGELOG.md) | Version history and changes |
| [JOINT_LIMITATIONS.md](./JOINT_LIMITATIONS.md) | Known limitations and workarounds |
| [TESTING_SUMMARY.md](./TESTING_SUMMARY.md) | Test coverage and results |

## Recall Kernel Adapter

libjoint is the time axis for the recall kernel (`rec.h` in libqmap; spec
in libqmap's `docs/RECALL-KERNEL.md`). The adapter is implemented
(`src/libjoint.c`, self-registered under the `"joint"` name, covered by
`src/test.c` Category 9) and follows the contract (one filler, streams
matches, seals, plain `int` return, additive):

```c
/* Exact [a,b) interval membership, entity id widened to rec_ref_t. */
int rec_axis_fill_interval(unsigned jd, time_t a, time_t b, rec_set_t *out);
```

## API Overview

| Function | Description |
|----------|-------------|
| `joint_init(fname)` | Create/open database (NULL = memory only) |
| `joint_start(jd, time, id)` | Record interval start for entity |
| `joint_stop(jd, time, id)` | Record interval stop for entity |
| `joint_iter(jd, min, max)` | Create query iterator |
| `joint_next(...)` | Get next result from iterator |

Splitting lives in the iterator itself (`joint_iter` decomposes the
window into constant-presence segments — see Operations below); there is
no separate `joint_split` function. See
[include/ttypt/joint.h](./include/ttypt/joint.h) for complete API
documentation.

## Operations

What libjoint stores and answers (see `QUICK_REFERENCE.md` for worked
patterns, `JOINT_LIMITATIONS.md` for limits):

- **Record presence**: `joint_start(jd, ts, id)` turns an entity's
  presence on (open interval, end = +infinity); `joint_stop(jd, ts, id)`
  turns it off. One entity may hold several disjoint intervals
  (stop, then start again). Entities are `unsigned` ids (`UINT32_MAX`
  reserved); timestamps are `time_t` in `[LONG_MIN/2, LONG_MAX/2]`.
- **Presence profile**: `joint_iter(jd, a, b)` + `joint_next` returns the
  window `[a,b)` decomposed into maximal segments where the *same set*
  of entities is present — each segment reports `min/max/count` plus its
  full id set (`count` successive `joint_next` calls). This one query
  shape serves every read:
  - *point presence* ("is X present at t"): `joint_iter(jd, t, t+1)`,
    match `who == X`;
  - *co-presence* ("when are 3 and 4 together"): iterate the range,
    keep segments whose id set contains both;
  - *concurrency* ("how many at once"): read `count` per segment;
  - *gaps* ("when is nobody present"): segments the iterator fills in
    with `count == 0`.
- **Kernel fill**: `rec_axis_fill_interval` (exact set of entities
  present in `[a,b)`). Filter-only — no ranker.
- **Not answered natively**: per-entity interval fetch ("all intervals
  of entity X" needs a full sweep; the `id` secondary index is internal
  only). Zero-duration intervals (`start == stop`) are stored but never
  matched, by design.

## Performance

Build flags: `-O3 -mpopcnt -mavx2 -mfma` (see `Makefile`). The query path
uses contiguous arenas for matches and splits plus an ephemeral
open-addressing entity set (no per-match/per-entity mallocs, no temp
qmap per gap). Record the numbers yourself:

```bash
make bench   # bin/test_extended with µs timing lines
```

Recent medians (µs, same loaded box, `-O0` baseline vs this tree):
query-all 10k 3.67M→1.60M (2.3×), 1k-overlap query 5.4k→326 (16.7×),
3k-overlap query 62k→1.3k (48.5×), 1000 insert/query cycles
180.8M→26.7M (6.8×), splits 343→62 (5.5×), inserts ~2×. Full table in
[CHANGELOG.md](./CHANGELOG.md) (`[Unreleased]`).

## Version

Current: **v1.2.1**

See [CHANGELOG.md](./CHANGELOG.md) for release history.

## License

See repository for details.

## Acknowledgments

- [qmap](https://github.com/tty-pt/qmap) - Underlying storage engine
- Leon - Debugging assistance
