# libjoint

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![interval-tree](https://img.shields.io/badge/interval-tree-16A34A)](#)

> Interval-tree time presence library.

A high-performance C library for storing and querying time-based intervals,
built on [corm](https://github.com/tty-pt/corm) for efficient sorted storage
and iteration.

## Contents

- [Features](#features)
- [Install](#install)
- [Build from source](#build-from-source)
- [Quickstart](#quickstart)
- [API overview](#api-overview)
- [Recall Kernel Adapter](#recall-kernel-adapter)
- [Operations](#operations)
- [Performance](#performance)
- [Documentation](#documentation)
- [Testing](#testing)
- [License](#license)

## Features

- **High capacity**: Store up to 65,536 intervals per database
- **High overlap support**: Handle up to 4,096 concurrent overlapping entities
- **Fast queries**: Microsecond-level query performance
- **File persistence**: Built on corm for reliable disk storage
- **Clean API**: Simple C interface with error handling
- **Zero-copy iteration**: Efficient traversal of query results

## Install

Prebuilt packages are distributed on tty.pt for Linux (APT / Alpine / Arch /
Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD.
Follow the [installation instructions](
https://github.com/tty-pt/ci/blob/main/docs/install.md) and use
**libjoint** as the package name.

## Build from source

The library builds with a plain `make` (the shared [`mk` include.mk](
https://github.com/tty-pt/mk)):

```sh
make                  # builds lib/libjoint.so (and friends)
make test             # run the in-tree test suite
sudo make install     # lib + headers + joint.pc -> $(PREFIX), default /usr/local
```

Link it from your own C code:

```sh
cc my_app.c $(pkg-config --cflags --libs joint)
```

**Dependencies:** `libcorm`, `libqsys` (+ libc, already on your system).
Both are built and linked automatically when you run `make`.

## Quickstart

```c
#include <ttypt/joint.h>

// Create database (NULL = memory only, "data.corm" = persisted)
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

## API overview

| Function | Description |
|----------|-------------|
| `joint_init(fname)` | Create/open database (NULL = memory only) |
| `joint_start(jd, time, id)` | Record interval start for entity |
| `joint_stop(jd, time, id)` | Record interval stop for entity |
| `joint_erase(jd, id)` | Erase every interval of an entity (absent → 0) |
| `joint_iter(jd, min, max)` | Create query iterator |
| `joint_next(...)` | Get next result from iterator |

Splitting lives in the iterator itself (`joint_iter` decomposes the
window into constant-presence segments — see Operations below); there is
no separate `joint_split` function. See
[include/ttypt/joint.h](./include/ttypt/joint.h) for complete API
documentation.

## Recall Kernel Adapter

libjoint is the time axis for the recall kernel (`rec.h` in libcorm; spec
in libcorm's `docs/RECALL-KERNEL.md`). The adapter is implemented
(`src/libjoint.c`, self-registered under the `"joint"` name, covered by
`src/test.c` Category 9) and follows the contract (one filler, streams
matches, seals, plain `int` return, additive):

```c
/* Exact [a,b) interval membership, entity id widened to rec_ref_t. */
int rec_axis_fill_interval(unsigned jd, time_t a, time_t b, rec_set_t *out);
```

Phase 2A store half (site
[mm-plan/PHASE-2-CLI.md](https://github.com/tty-pt/site/blob/main/mm-plan/PHASE-2-CLI.md)
2A-3, **DONE 2026-09-14**): `joint_erase(jd, id)` removes every interval an
entity owns (id-index walk → collect → primary del each → `corm_del_all` mop;
absent → 0; `UINT32_MAX` → `EINVAL`; zero new stored state) plus the
`rec_axis_store`/`unstore`/`readback` adapters. The store parses the whole
value string in its own ordered grammar — `"A"` or `"<DATE>:<anything>"`
open, `",B"` close-or-backfill, `"A,B"` (`B>A`) atomic, else `EINVAL` —
with an id-index exact-match guard so exact-duplicate restates are no-ops;
read-back is one NUL-joined buffer of entries in the same grammar. Covered
by `src/joint_axis_store_test.c` (116 assertions, wired into `test.sh`).

> Note: a `jd` of 0 widens to a NULL `ctx` pointer, which the locked
> store contract rejects — an in-memory store opened first (handle 0) is
> unreachable through the adapters; open the adapter store on a nonzero
> handle.

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
- **Per-entity fetch + erase**: `rec_axis_readback` (the entity's
  intervals in the store grammar, NUL-joined) and `joint_erase` (remove
  them all) walk the internal `id` index — O(intervals-of-entity).
- **Not answered natively**: zero-duration intervals (`start == stop`)
  are stored but never matched, by design.

## Performance

Build flags: `-O3 -mpopcnt -mavx2 -mfma` (see `Makefile`). The query path
uses contiguous arenas for matches and splits plus an ephemeral
open-addressing entity set (no per-match/per-entity mallocs, no temp
corm per gap). Record the numbers yourself:

```bash
make bench   # bin/test_extended with µs timing lines
```

Recent medians (µs, same loaded box, `-O0` baseline vs this tree):
query-all 10k 3.67M→1.60M (2.3×), 1k-overlap query 5.4k→326 (16.7×),
3k-overlap query 62k→1.3k (48.5×), 1000 insert/query cycles
180.8M→26.7M (6.8×), splits 343→62 (5.5×), inserts ~2×. Full table in
[CHANGELOG.md](./CHANGELOG.md) (`[Unreleased]`).

## Documentation

| Document | Description |
|----------|-------------|
| [QUICK_REFERENCE.md](./QUICK_REFERENCE.md) | API overview with code examples |
| [CHANGELOG.md](./CHANGELOG.md) | Version history and changes |
| [JOINT_LIMITATIONS.md](./JOINT_LIMITATIONS.md) | Known limitations and workarounds |
| [TESTING_SUMMARY.md](./TESTING_SUMMARY.md) | Test coverage and results |

## Testing

Run the in-tree suite:

```sh
make test
```

## Acknowledgments

- [corm](https://github.com/tty-pt/corm) — underlying storage engine
- Leon — debugging assistance

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt. See `LICENSE`.