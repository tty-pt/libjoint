# Changelog

All notable changes to libjoint will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed — sweep-line performance tiers (query path ~2–48× faster)

Public API unchanged. All work is in the query/split path
(`joint_iter` → `joint_next`); write path (`joint_start`/`joint_stop`) untouched.

- **T0**: `CFLAGS += -O3 -mpopcnt -mavx2 -mfma` (the library previously
  built at `-O0`); new `make bench` target runs `bin/test_extended`
  with µs timing lines.
- **T1**: `isplit_cmp` no longer `memcpy`s 16 bytes per comparison —
  direct pointer-cast field compare (≈2M·log(2M) comparisons per query).
- **T2**: per-match `malloc` in `ti_intersect` replaced by a
  `realloc`-grown contiguous `struct match_arena` (bulk-freed per query).
- **T3**: per-split `malloc` + per-entity `ids_push` (one malloc each)
  replaced by a query-level block-list arena (`struct split_arena`,
  blocks never move so TAILQ pointers and `split->ids` stay stable
  across `splits_fill` recursion); split id lists are contiguous
  `uint32_t` LIFO stacks. Dead `splits_free` removed.
- **T4**: the temp per-gap corm (`corm_open`/`corm_close` on every
  `splits_get`) replaced by an ephemeral open-addressing `who_set`
  (tombstone deletions, count maintained so `split_create` is
  single-pass); `SPLITS_WHO_MASK` retired.

### Fixed

- Two latent signed-integer overflows in `src/test_extended.c` exposed
  by `-Waggressive-loop-optimizations`/UBSan: `1700000000 + i * 86400`
  (overflows `int` at i=5180) → `time_t` arithmetic; query end
  `huge + 1000` (overflows int64) → `huge` (same coverage, interval
  ends at `huge`).

### Performance (medians, µs; `make bench` on one loaded box)

Baseline = pristine `-O0` build (medians of 3); optimized = this tree
(medians of 9). Same machine, same load window.

| Bench | Baseline | Now | × |
|---|---|---|---|
| 10k insert | 35 734 | 16 137 | 2.2 |
| query-all (10k) | 3 673 044 | 1 601 764 | 2.3 |
| 1k overlap insert | 9 234 | 5 258 | 1.8 |
| 1k overlap query | 5 444 | 326 | 16.7 |
| sequential 5k | 2 267 215 | 1 287 717 | 1.8 |
| sparse query | 81 | 13 | 6.2 |
| splits (100 ent) | 343 | 62 | 5.5 |
| 1000 insert/query cycles | 180 756 539 | 26 737 804 | 6.8 |
| 15k insert | 45 691 | 21 613 | 2.1 |
| 15k range query | 2 034 606 | 997 656 | 2.0 |
| 3k overlap insert | 52 867 | 30 614 | 1.7 |
| 3k overlap query | 62 079 | 1 281 | 48.5 |
| 20k insert | 93 440 | 33 995 | 2.7 |
| 20k range query | 1 683 350 | 801 620 | 2.1 |

Absolute numbers swing 2–5× with machine load (both columns measured in
the same window, so ratios are like-for-like). All 74 tests pass
(`./test.sh`); ASan+UBSan clean (manual build, `detect_leaks=0` — the
iterator arena is intentionally never freed; no teardown API exists, as
before). Valgrind out of scope per project policy.

## [1.2.2] - 2026-09-10

### Fixed — W3 index-read efficiency regression

- Equality reads (`ti_present`, `ti_finish_last`) now use
  `corm_get_multi()` instead of `corm_iter(…, CM_RANGE)`: the same
  duplicate-set iteration but O(k) per key (chain walk) with no
  full-table unsorted-to-sorted rebuild of the MV indexes.
  libcorm ≥ 0.8.0 (chains) / ≥ 0.8.0-backshift (no-holes) required;
  compat floors otherwise unchanged (≥ `1a4bfa2` auto-grow,
  ≥ `b1bc322` assoc-multivalue).
- No public `joint_*` behavior changed; `ti_intersect` GE scans on `max`
  unchanged.

### Performance (vs pre-W3 baseline, new libcorm)

| Bench | Baseline | Now |
|---|---|---|
| 10k start+stop | 2 499 442 µs | **14 297 µs** |
| query-all | 1 833 316 µs | **872 228 µs** |
| sequential | 579 107 µs | **573 451 µs** |
| interleave | 12 981 µs | **998 µs** |
| 15k insert | 5 928 149 µs | **17 749 µs** |
| 20k insert | 9 131 138 µs | **18 124 µs** |
| 1000-cycle | 100 342 259 µs | **71 072 518 µs** |

**Test count:** still 74 (59 core + 15 extended), all passing.

---

## [1.2.1] - 2026-02-23

### Fixed - File Persistence

**File Persistence Now Works** ✅
- Removed CM_MIRROR flag from `joint_init()` (src/libjoint.c:166)
- **Root cause:** corm v0.7.0+ no longer requires CM_MIRROR for file persistence
- **Solution:** Changed flags from `CM_MIRROR` to `0`
- **Why it works:** libjoint doesn't need bidirectional lookups (corm_assoc), so CM_MIRROR was unnecessary

**Test Results:**
- All 5 Category 7 persistence tests now passing:
  - test_persist_save_load ✅
  - test_persist_multiple_intervals ✅
  - test_persist_empty_database ✅
  - test_persist_append ✅
  - test_persist_large_dataset ✅

**Total Test Count:** 74 tests (59 core + 15 extended)

### Documentation Updated
- JOINT_LIMITATIONS.md: Added section 5 for persistence fix
- CORM_PERSISTENCE_BUGS.md: Marked as resolved

---

## [1.2.0] - 2026-02-23

### Fixed - Design Limitations Addressed

**TI_MASK Limit (Fixed)**
- Increased from `0x7FF` to `0xFFFF` (src/libjoint.c:28)
- **Old limit:** ~2,048 intervals per database
- **New limit:** 65,536 intervals per database  
- **Increase:** 32x capacity
- Test coverage: 10k, 15k, and 20k interval tests passing

**SPLITS_WHO_MASK Limit (Fixed)**
- Increased from `0xFF` to `0xFFF` (src/libjoint.c:27)
- **Old limit:** 256 entities per split interval
- **New limit:** 4,096 entities per split interval
- **Increase:** 16x capacity
- Test coverage: 1k and 3k overlapping entity tests passing

**Extreme Timestamps (Fixed)**
- Added input validation to `joint_start()` and `joint_stop()`
- **Valid range:** `[LONG_MIN/2, LONG_MAX/2]`
- **Behavior:** Returns -1 with `errno = ERANGE` for out-of-range timestamps
- **Rationale:** Prevents conflicts with internal sentinel values (mtinf, tinf)
- Test coverage: Category 8 validation tests

**UINT32_MAX Entity ID (Fixed)**
- Added input validation to reject UINT32_MAX
- **Behavior:** Returns -1 with `errno = EINVAL` for UINT32_MAX entity ID
- **Rationale:** Prevents conflicts with IDM_MISS sentinel (0xFFFFFFFF)
- **Valid range:** 0 to 4,294,967,294 (0x00000000 to 0xFFFFFFFE)
- Test coverage: Category 8 validation tests

### Changed - API Behavior

**joint_start() and joint_stop() Return Values**
- **Previous:** 0=success, 1=duplicate/no-interval
- **New:** 0=success, 1=duplicate/no-interval, -1=validation error
- **Error reporting:** Check `errno` when return value is -1
  - `ERANGE`: Timestamp outside valid range
  - `EINVAL`: Entity ID is UINT32_MAX

### Added

**Category 8: Input Validation Tests** (4 new tests)
- test_validation_extreme_timestamp_start: Validates timestamp range in joint_start()
- test_validation_extreme_timestamp_stop: Validates timestamp range in joint_stop()
- test_validation_uint32_max_start: Validates entity ID in joint_start()
- test_validation_uint32_max_stop: Validates entity ID in joint_stop()

**Extended Test Suite Enhancements** (3 new boundary tests)
- Test 13: 15,000 intervals (approaching 65k limit)
- Test 14: 3,000 overlapping entities (approaching 4k limit)  
- Test 15: 20,000 intervals (30% of TI_MASK limit)

**Updated test limits:**
- Test 1: 2,000 → 10,000 intervals
- Test 2: 250 → 1,000 overlapping entities

**Total test count:** 69 tests (54 core + 15 extended)

### Updated Documentation

- **JOINT_LIMITATIONS.md**: Marked 4 limitations as FIXED, reorganized by status
- **QUICK_REFERENCE.md**: Updated limits table and error handling examples
- **TESTING_SUMMARY.md**: Added v1.2.0 fixes section
- **joint.h**: Updated API documentation for joint_start() and joint_stop() with new return codes

### Known Issues

**Zero-Duration Intervals (Not Fixed - By Design)**
- Intervals where start==stop remain unsupported
- Query mechanism uses exclusive upper bounds, making point-in-time queries inconsistent
- Workaround: Use minimum duration of 1 time unit

**Persistence Tests (Still Failing)**
- Re-tested with corm b1bc322 (includes df5a7ac file loading fix)
- Result: Segmentation fault - bugs still present
- Category 7 tests remain disabled pending upstream corm fixes
- See CORM_PERSISTENCE_BUGS.md for updated test results

### Performance

Performance remains excellent with increased limits:
- 10k intervals: 350.86 µs/interval insertion
- 1k overlapping entities: 19.8 ms total creation time
- 20k intervals: 589.65 µs/interval insertion
- Query performance: <1ms for most queries

### Compatibility

- **corm version:** b1bc322+ (tested with b1bc322)
- **Breaking changes:** None - backward compatible API
- **New behavior:** Validation errors return -1 (previously would silently fail or corrupt data)

## [1.1.0] - 2026-02-23

### Changed
- Align project structure with corm v0.6.0 patterns
- Update .gitignore with build artifact and test file patterns (/bin, /*.db, /*.corm, /man)
- Add Doxygen support for automatic man page generation
- Enhance API documentation in joint.h with comprehensive Doxygen comments
- Update dependencies in joint.pc (libcorm, libqsys instead of libqdb, libdb)
- Establish CHANGELOG for version tracking

### Fixed
- Fixed missing corm_fin() calls in ti_intersect() and split_create() causing memory leaks (Phase 2)
- Fixed integer underflow bug in splits_create() when matches_l=0 causing infinite loop and corruption (Phase 2)
- Added proper empty result handling in splits_get() for query ranges with no matches (Phase 2)
- Fixed errno not being reset in sscantime() before strtoull() call, causing false positives (Phase 3 - src/libjoint.c:87)
- Fixed printtime() missing return statements after setting "-inf"/"inf", preventing crashes on extreme timestamps (Phase 3 - src/libjoint.c:106, 111)

### Added
- Comprehensive test suite (Phases 1, 2, 3 & 4):
  - Category 1: Basic initialization tests (3 tests)
  - Category 2: Basic start/stop operation tests (10 tests)
  - Category 3: Multiple entities tests (15 tests)
  - Category 4: Intersection query tests (8 tests)
  - Category 5: Split computation tests (8 tests)
  - Category 6: Time utilities tests (6 tests - sscantime, printtime)
  - Category 7: Persistence tests (5 tests - DISABLED due to corm bugs)
  - **Category 8: Extended tests** (12 tests - stress, performance, edge cases):
    - Test 1: Large dataset stress test (2000 intervals)
    - Test 2: Many overlapping intervals (250 entities)
    - Test 3: Sequential insertion performance benchmark
    - Test 4: Query performance on sparse data
    - Test 5: Extreme timestamp values (INT64_MAX, negative timestamps)
    - Test 6: Split computation performance (100 overlapping entities)
    - Test 7: Repeated operations (memory leak detection)
    - Test 8: Boundary query performance
    - Test 9: Entity ID edge cases (ID=0, ID=UINT32_MAX)
    - Test 10: Time utility functions stress test (10,000 parse/format operations)
    - Test 11: Interleaved operations stress test (500 entities)
    - Test 12: Zero-duration intervals (start == stop)
- Total: 62 automated tests covering all functionality plus performance benchmarks
- test_extended binary with performance metrics (microsecond timing, throughput reporting)
- Integration test script (test.sh) with regression testing via expects.txt
- Test output formatting with ✅/❌ indicators for easy visual verification
- Added joint_close() function for future persistence support (currently no-op for in-memory databases)

### Notes
- Code is fully compatible with corm v0.6.0 (updated in v1.0.0)
- Benefits from corm v0.6.0 improvements:
  - Improved pointer stability (allocation reuse)
  - Automatic file loading for persistence
  - Enhanced corm documentation
- Note: File persistence issues were resolved in v1.2.1

---

## [1.0.0] - 2025-10-20
## [1.0.0] - 2025-10-26

### Added
- Initial stable release
- Interval tree implementation using corm v0.5.0+
- File-based persistence support via corm
- Multiple database support per file
- Sorted iteration via CM_SORTED
- BTREE secondary indexes for efficient queries (by max time, by ID)
- ISO-8601 date string parsing and formatting utilities

### Changed
- Migrated from libqdb to libcorm
- Updated from `unsigned` to `uint32_t` types throughout
- Adopted corm's persistent storage model
- Reorganized headers to ttypt/ namespace
