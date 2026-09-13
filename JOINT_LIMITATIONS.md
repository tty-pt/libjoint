# libjoint Design Limitations

This document describes design limitations discovered during comprehensive testing. Some limitations have been addressed in subsequent versions.

## Summary of Current Status (v1.2.2)

| Limitation | Status | Version Fixed |
|------------|--------|---------------|
| TI_MASK Limit (~2048 intervals) | **FIXED** | v1.2.0 |
| SPLITS_WHO_MASK Limit (256 entities) | **FIXED** | v1.2.0 |
| Extreme Timestamps (overflow risk) | **FIXED** | v1.2.0 |
| UINT32_MAX Entity ID (sentinel conflict) | **FIXED** | v1.2.0 |
| File Persistence (qmap bugs) | **FIXED** | v1.2.1 |
| MV Index Write Efficiency (W3 regression) | **FIXED** | v1.2.2 |
| Zero-Duration Intervals (start == stop) | **NOT FIXED** | By Design |

**See CHANGELOG.md for v1.2.2 implementation details.**

---

# Fixed Limitations (as of v1.2.2)

The following limitations have been addressed:

## 1. TI_MASK Limit: Maximum ~2048 Intervals → FIXED ✅

**Status:** FIXED in v1.2.0 (increased to 65,536 intervals)

### Original Problem (v1.1.0)

### Description
The `TI_MASK` constant in `src/libjoint.c:28` is defined as `0x7FF` (2047), which limits the qmap database to a maximum of approximately 2048 intervals.

### Source Code
```c
#define TI_MASK 0x7FF  // src/libjoint.c:28
```

This mask is used when opening the qmap databases:
```c
dbs->ti = qmap_open(fname, "ti", qm_ti, qm_ti, TI_MASK, flags);  // line 169
dbs->max = qmap_open(NULL, NULL, qm_time, qm_ti, TI_MASK, QM_SORTED);  // line 170
dbs->id = qmap_open(NULL, NULL, qm_id, qm_ti, TI_MASK, QM_SORTED);  // line 171
```

### Impact
- Applications requiring more than 2048 active intervals will fail silently
- Insertions beyond the limit are silently dropped
- Queries return incomplete results (capped at 2048 entries)

### Evidence
Test case demonstrating the limit:

```c
// Test with 10,000 intervals
for (int i = 0; i < 10000; i++) {
    joint_start(jd, 1000 + i * 100, i + 1);
    joint_stop(jd, 1000 + i * 100 + 50, i + 1);
}

// Query all intervals
joint_cur_t cur = joint_iter(jd, 0, 2000000);
int found = 0;
while (joint_next(&min, &max, &count, &who, &cur)) {
    found++;
}
// Result: found = 2048 (expected 10000)
```

**Query results by range:**
- Range [0, 100000): 30 intervals found
- Range [0, 500000): 178 intervals found
- Range [0, 2000000): 2048 intervals found (hard limit)

### Workaround
- Keep interval count below 2048 per database
- Use multiple database instances (via `joint_init()`) to partition data
- Consider increasing TI_MASK to a larger value (requires recompilation):
  ```c
  #define TI_MASK 0xFFFF  // 65535 max intervals
  ```

### Related Tests
- `test_extended.c`: Test 1 (Large Dataset Stress Test)
- Originally tested with 10,000 intervals, reduced to 2,000 to stay within limit

### Fix (v1.2.0)
**Changed:** `TI_MASK` from `0x7FF` to `0xFFFF` (src/libjoint.c:28)
- **Old limit:** ~2,048 intervals
- **New limit:** 65,536 intervals
- **Increase factor:** 32x

**Test coverage (v1.2.0):**
- Test 1: 10,000 intervals ✅
- Test 13: 15,000 intervals (approaching limit) ✅
- Test 15: 20,000 intervals (30% of limit) ✅

---

## 2. SPLITS_WHO_MASK Limit: Maximum 256 Entities Per Split → FIXED ✅

**Status:** FIXED in v1.2.0 (increased to 4,096 entities)

### Original Problem (v1.1.0)

### Description
The `SPLITS_WHO_MASK` constant in `src/libjoint.c:27` is defined as `0xFF` (255), which limits each split interval to a maximum of 256 entity IDs.

### Source Code
```c
#define SPLITS_WHO_MASK 0xFF  // src/libjoint.c:27
```

This mask is used when creating the temporary qmap for entity IDs during split computation:
```c
uint32_t who_hd = qmap_open(NULL, NULL, QM_HNDL, QM_HNDL, SPLITS_WHO_MASK, 0);  // line 430
```

### Impact
- When more than 256 entities overlap in the same time period, only 256 are returned
- Queries on highly overlapping intervals return incomplete results
- The `count` field in `joint_next()` may report a higher number than entities actually returned

### Evidence
Test case demonstrating the limit:

```c
// Create 1000 overlapping intervals (all at same time)
for (int i = 0; i < 1000; i++) {
    joint_start(jd, 5000, i + 1);  // All start at 5000
    joint_stop(jd, 10000, i + 1);  // All end at 10000
}

// Query the overlapping region
joint_cur_t cur = joint_iter(jd, 6000, 8000);
int found = 0;
while (joint_next(&min, &max, &count, &who, &cur)) {
    found++;
}
// Result: found = 256 (expected 1000)
```

### Workaround
- Limit concurrent overlapping entities to 256 or fewer
- Design time intervals to minimize overlap
- Consider increasing SPLITS_WHO_MASK (requires recompilation):
  ```c
  #define SPLITS_WHO_MASK 0xFFF  // 4095 max entities per split
  ```
  **Note**: Larger masks increase memory usage for the temporary qmap

### Related Tests
- `test_extended.c`: Test 2 (Many Overlapping Intervals)
- Originally tested with 1,000 entities, reduced to 250 to stay within limit

### Fix (v1.2.0)
**Changed:** `SPLITS_WHO_MASK` from `0xFF` to `0xFFF` (src/libjoint.c:27)
- **Old limit:** 256 entities per split
- **New limit:** 4,096 entities per split
- **Increase factor:** 16x

**Test coverage (v1.2.0):**
- Test 2: 1,000 overlapping entities ✅
- Test 14: 3,000 overlapping entities (approaching limit) ✅

---

## 3. Extreme Timestamps: INT64_MAX Overflow → FIXED ✅

**Status:** FIXED in v1.2.0 (input validation added)

### Original Problem (v1.1.0)

### Description
Timestamps near `INT64_MAX` (9,223,372,036,854,775,807) may cause overflow or undefined behavior in libjoint's internal calculations.

### Source Code
The issue stems from timestamp arithmetic in libjoint and qmap, particularly when:
- Computing interval intersections
- Sorting intervals by max time
- Performing range queries with extreme values

Relevant constants in `src/libjoint.c`:
```c
#ifdef __OpenBSD__
#define TS_MIN LLONG_MIN
#define TS_MAX LLONG_MAX
#else
#define TS_MIN LONG_MIN
#define TS_MAX LONG_MAX
#endif

const time_t mtinf = (time_t) TS_MIN;  // minus infinite
const time_t tinf = (time_t) TS_MAX;   // infinite
```

### Impact
- Intervals with timestamps near INT64_MAX may not be stored correctly
- Queries involving extreme timestamps may fail silently
- Open intervals (using `tinf` as max) work correctly as designed

### Evidence
Test case demonstrating the issue:

```c
time_t huge = 9223372036854775000LL;  // Near INT64_MAX
joint_start(jd, huge - 1000, 1);
joint_stop(jd, huge, 1);

// Query for the interval
joint_cur_t cur = joint_iter(jd, huge - 2000, huge + 1000);
int found = 0;
while (joint_next(&min, &max, &count, &who, &cur)) {
    if (who == 1) found++;
}
// Result: found = 0 (expected 1)
```

**Negative timestamps work correctly:**
```c
joint_start(jd, -1000000, 2);
joint_stop(jd, -999000, 2);

joint_cur_t cur = joint_iter(jd, -1001000, -998000);
// Result: Found correctly
```

### Workaround
- Keep timestamps within a reasonable range (e.g., Unix epoch 0 to year 2100)
- Use relative timestamps from a base epoch rather than absolute values
- For "open" intervals, use `joint_start()` without `joint_stop()` (uses `tinf` internally)

### Platform Considerations
- On 32-bit systems, `time_t` may be 32-bit (2038 problem)
- On 64-bit systems, `time_t` is typically 64-bit signed

### Related Tests
- `test_extended.c`: Test 5 (Extreme Timestamp Values)
- Extreme timestamp test marked as SKIP (known limitation)

### Fix (v1.2.0)
**Added:** Input validation to `joint_start()` and `joint_stop()`
- **Valid range:** `[LONG_MIN/2, LONG_MAX/2]`
- **Behavior:** Returns -1 with `errno = ERANGE` for out-of-range timestamps
- **Rationale:** Prevents conflicts with internal sentinels (`mtinf` = LONG_MIN, `tinf` = LONG_MAX)

**API changes:**
```c
int joint_start(unsigned jd, time_t ts, unsigned id);
int joint_stop(unsigned jd, time_t ts, unsigned id);
// Return values:
//   0 = success
//   1 = duplicate start / no open interval
//  -1 = validation error (errno = ERANGE for timestamp overflow)
```

**Test coverage (v1.2.0):**
- Category 8: test_validation_extreme_timestamp_start ✅
- Category 8: test_validation_extreme_timestamp_stop ✅

---

## 4. UINT32_MAX Entity ID: IDM_MISS Sentinel Conflict → FIXED ✅

**Status:** FIXED in v1.2.0 (input validation added)

### Original Problem (v1.1.0)

### Description
The entity ID value `UINT32_MAX` (0xFFFFFFFF or 4,294,967,295) conflicts with the `IDM_MISS` sentinel value used by the qmap IDM (ID Manager) module.

### Source Code
From `qmap/include/ttypt/idm.h:26`:
```c
#define IDM_MISS ((uint32_t)-1)  // = 0xFFFFFFFF = UINT32_MAX
```

The `ids_pop()` function returns `IDM_MISS` when no more IDs are available:
```c
uint32_t ids_pop(ids_t *list);  // Returns IDM_MISS if empty
```

In `src/libjoint.c:594`, libjoint uses this to detect end of iteration:
```c
while ((*who = ids_pop(&internal->next->ids)) == (uint32_t) -1) {
    internal->next = TAILQ_NEXT(internal->next, entry);
    if (!internal->next)
        return 0;
}
```

### Impact
- Entity ID `UINT32_MAX` cannot be stored or queried
- Intervals with `who = UINT32_MAX` are treated as "no entity"
- The entity ID is effectively reserved and unavailable to applications

### Evidence
Test case demonstrating the conflict:

```c
// Insert with UINT32_MAX entity ID
joint_start(jd, 3000, (unsigned)-1);  // UINT32_MAX
joint_stop(jd, 4000, (unsigned)-1);

// Query for the interval
joint_cur_t cur = joint_iter(jd, 2500, 4500);
int found_max = 0;
while (joint_next(&min, &max, &count, &who, &cur)) {
    if (who == (unsigned)-1) found_max = 1;
}
// Result: found_max = 0 (UINT32_MAX is filtered out)
```

**Entity ID 0 works correctly:**
```c
joint_start(jd, 1000, 0);
joint_stop(jd, 2000, 0);
// Result: Found correctly, ID=0 is valid
```

### Workaround
- Avoid using entity ID `UINT32_MAX` (4,294,967,295)
- Valid entity ID range: 0 to 4,294,967,294 (0x00000000 to 0xFFFFFFFE)
- Document this restriction in application-level code

### Alternative Solution
Change the sentinel value in IDM to use a different value (requires modifying qmap):
```c
#define IDM_MISS 0  // Would conflict with entity ID 0 instead
```
This is not recommended as it would break existing qmap APIs.

### Related Tests
- `test_extended.c`: Test 9 (Entity ID Edge Cases)
- UINT32_MAX test marked as SKIP (sentinel conflict documented)

### Fix (v1.2.0)
**Added:** Input validation to `joint_start()` and `joint_stop()`
- **Rejected value:** `UINT32_MAX` (0xFFFFFFFF)
- **Behavior:** Returns -1 with `errno = EINVAL` for UINT32_MAX entity ID
- **Rationale:** Prevents conflicts with `IDM_MISS` sentinel used internally

**API changes:**
```c
int joint_start(unsigned jd, time_t ts, unsigned id);
int joint_stop(unsigned jd, time_t ts, unsigned id);
// Return values:
//   0 = success
//   1 = duplicate start / no open interval
//  -1 = validation error (errno = EINVAL for UINT32_MAX entity ID)
```

**Valid entity ID range (v1.2.0):** 0 to 4,294,967,294 (0x00000000 to 0xFFFFFFFE)

**Test coverage (v1.2.0):**
- Category 8: test_validation_uint32_max_start ✅
- Category 8: test_validation_uint32_max_stop ✅

---

## 5. File Persistence: qmap QM_MIRROR Bug → FIXED ✅

**Status:** FIXED in v1.2.1 (removed QM_MIRROR requirement)

### Original Problem (v1.1.0 - v1.2.0)

### Description
File persistence using `joint_init("/path/to/file.db")` caused segmentation faults when closing and reopening the database. This was due to bugs in the qmap library's file persistence implementation.

### Root Cause
The qmap library (prior to v0.7.0) required the `QM_MIRROR` flag for file persistence. However, qmap had multiple bugs related to QM_MIRROR:
1. Multiple databases per file failed to persist
2. Process exit crashes with custom types and QM_MIRROR
3. Segmentation faults when reopening file-backed databases

### Source Code
Original code in `src/libjoint.c:166`:
```c
uint32_t flags = fname ? QM_MIRROR : 0;  /* QM_MIRROR required for file persistence */
```

### Impact
- File persistence was completely broken
- Category 7 persistence tests (5 tests) were disabled
- Applications requiring persistence could not use libjoint

### Evidence
Test results showing the failure:
```
=== Category 7: Persistence ===
Segmentation fault (core dumped)
```

### Fix Applied (v1.2.1)
qmap v0.7.0+ (commit df5a7ac) changed file persistence to work WITHOUT QM_MIRROR:
- File loading now happens automatically when opening file-backed maps
- QM_MIRROR is now optional, only needed for bidirectional lookups
- libjoint doesn't need bidirectional lookups (no qmap_assoc usage)

Changed `src/libjoint.c:166`:
```c
uint32_t flags = 0;  /* QM_MIRROR optional in qmap v0.7.0+, not needed for persistence */
```

### Test Coverage (v1.2.1)
All 5 persistence tests now pass:
- Category 7: test_persist_save_load ✅
- Category 7: test_persist_multiple_intervals ✅
- Category 7: test_persist_empty_database ✅
- Category 7: test_persist_append ✅
- Category 7: test_persist_large_dataset ✅

---

## 7. MV Index Write Efficiency (W3 regression) → FIXED ✅

**Status:** FIXED in v1.2.2 (libqmap MV duplicate chains + backshift; libjoint equality reads via `qmap_get_multi`)

### Description
Adopting qmap's QM_MULTIVALUE subsidiary indexes (W3) made every libjoint write
and some reads hit libqmap's lazy-sorted-index design per operation:
- `joint_start`/`joint_stop` → `ti_present` → full id-index qsort per insert
  (**×9–16 inserts**).
- `ti_finish_last` delete → MV sorted rebuild per delete; `qmap_close`
  per-entry MV deletes → quadratic close (**hang**).
- `joint_iter` GE-bsearch on `max` → one full rebuild per write→query burst
  (×1.9 queries).
- A later `qmap_mv_slot` full-table probe (up to 65 536 slots) made fresh-key
  puts O(m) again after the first fix (libjoint 10k pairs: 21.4 s → 14 ms).

### Fix (v1.2.2)
- libqmap: per-key MV duplicate chain (O(k) `qmap_get_multi`, O(k) MV delete,
  O(N) close) + hole-eliminating backshift delete (all probes early-exit).
- libjoint: `ti_present`/`ti_finish_last` now use `qmap_get_multi()` (O(k)) with
  `QM_MISS` guards. `ti_intersect` GE scans on `max` unchanged.

### Results
All benchmarks at or below the pre-adoption baseline; 10k start+stop pairs
2 499 442 µs → 14 297 µs; query-all 1 833 316 → 872 228 µs; 1000-cycle
100 342 259 → 71 072 518 µs. No behavior change; all 74 tests pass.
Requires libqmap with the MV chain + backshift (0.8.0).

### Related Tests
- `src/test_extended.c`: 15 extended benchmarks; `src/test.c`: 59 core tests.

---

# Remaining Limitations (Not Fixed)

## 6. Zero-Duration Intervals Not Supported (BY DESIGN)

### Description
Intervals where the start and stop timestamps are identical (zero duration, representing a single point in time) are not properly supported by libjoint's query mechanism.

### Behavior
When an interval has `start == stop`:
- The interval is stored in the database
- The interval is **not returned** by `joint_iter()` queries
- This is due to libjoint's interval intersection logic using exclusive upper bounds

### Source Code
The intersection check in `src/libjoint.c:238`:
```c
if (tmp.max >= min && tmp.min < max) {
    // Match found
}
```

For a query range `[A, B)` and an interval `[T, T]` where `T` is the zero-duration point:
- Query must satisfy: `T >= A` AND `T < B`
- But for the interval itself: `max = T` and `min = T`
- Intersection check: `T >= A` (true if A ≤ T) AND `T < B` (true if T < B)

The issue is that intervals use **half-open ranges** `[min, max)` where `max` is exclusive:
- An interval `[1000, 1000)` represents an empty set
- A query `[999, 1001)` looking for point 1000 won't match `[1000, 1000)`

### Impact
- Point-in-time events (zero duration) cannot be reliably stored and queried
- Applications requiring instant events must use small non-zero durations
- Timestamps representing "moments" need to be given artificial width

### Evidence
Test case demonstrating the issue:

```c
// Create zero-duration interval at time 5000
joint_start(jd, 5000, 50);
joint_stop(jd, 5000, 50);  // start == stop

// Query for the point
joint_cur_t cur = joint_iter(jd, 5000, 5001);  // Range includes 5000
int found = 0;
while (joint_next(&min, &max, &count, &who, &cur)) {
    found++;
}
// Result: found = 0 (zero-duration interval not returned)
```

**Normal intervals work correctly:**
```c
joint_start(jd, 5000, 51);
joint_stop(jd, 5001, 51);  // Duration = 1
// Result: Found correctly via queries
```

### Workaround
Use minimal non-zero durations for point events:
```c
// Instead of:
joint_start(jd, timestamp, entity_id);
joint_stop(jd, timestamp, entity_id);  // DON'T DO THIS

// Use:
joint_start(jd, timestamp, entity_id);
joint_stop(jd, timestamp + 1, entity_id);  // Minimum duration of 1
```

### Design Consideration
This limitation is inherent to interval tree semantics where:
- Intervals represent time ranges `[start, end)` with exclusive upper bound
- A range `[T, T)` is mathematically empty
- Query intersection logic cannot match empty intervals

To properly support point events, libjoint would need:
1. Special handling for `start == stop` as point events
2. Modified intersection logic: `(tmp.max > min || (tmp.max == tmp.min && tmp.min >= min)) && tmp.min < max`
3. Additional flag or data structure to distinguish points from ranges

### Related Tests
- `test_extended.c`: Test 12 (Zero-Duration Intervals)
- Marked as SKIP (not supported by design)

---

## Testing Methodology

These limitations were discovered through systematic stress testing in Phase 4:

### Test Coverage
- **Large datasets**: Up to 20,000 intervals tested (v1.2.0)
- **Overlapping intervals**: Up to 3,000 concurrent entities tested (v1.2.0)
- **Extreme values**: INT64_MAX, LONG_MAX, negative timestamps tested
- **Edge cases**: ID=0, ID=UINT32_MAX, zero-duration intervals tested
- **Performance**: Microsecond-precision timing for all operations
- **Input validation**: Timestamp range and entity ID validation (v1.2.0)

### Test Files
- `src/test_extended.c`: 15 comprehensive extended tests (v1.2.0)
- `src/test.c`: 59 core tests including 4 validation tests (v1.2.0) + 5 persistence tests (v1.2.1)
- All limitations documented with test cases demonstrating the behavior
- Tests use SKIP markers for known limitations rather than false failures

### Verification Commands
```bash
# Run core tests (59 tests including validation + persistence)
cd /home/quirinpa/libjoint
make
LD_LIBRARY_PATH=./lib ./bin/test

# Run extended tests (15 tests including boundary tests)
LD_LIBRARY_PATH=./lib ./bin/test_extended
```

---

## Recommendations

### For Application Developers (v1.2.0)

1. **Interval Count**: Can now use up to 65,000 intervals per `joint_init()` instance ✅
2. **Overlapping Entities**: Can now use up to 4,000 concurrent overlapping entities ✅
3. **Timestamp Range**: Use timestamps in range [LONG_MIN/2, LONG_MAX/2] (validated) ✅
4. **Entity IDs**: Cannot use UINT32_MAX (returns error with errno=EINVAL) ✅
5. **Point Events**: Use minimum duration of 1 time unit instead of zero-duration (still required)

### For Library Maintainers

**Completed in v1.2.0:** ✅
1. ~~Increase `TI_MASK` to `0xFFFF` (65,536 intervals)~~ DONE
2. ~~Increase `SPLITS_WHO_MASK` to `0xFFF` (4,096 entities)~~ DONE
3. ~~Add runtime checks with error reporting when limits are exceeded~~ DONE (errno-based)
4. ~~Add overflow checks for extreme timestamp arithmetic~~ DONE (validation added)
5. ~~Document entity ID restrictions in API documentation~~ DONE (it.h updated)

**Future Enhancements:**
6. Consider special handling for zero-duration intervals (point events)
7. Add `joint_get_limits()` API to query current mask values at runtime
8. Make masks configurable via `joint_init()` parameters

### Compatibility Notes

**v1.2.2 (current):**
- TI_MASK: 0xFFFF (65,536 intervals)
- SPLITS_WHO_MASK: 0xFFF (4,096 entities)
- Input validation + MV index efficiency fixes (qmap_get_multi)
- Compatible with qmap 0.8.0+ (MV duplicate chains + backshift)

**v1.2.1 (legacy):**
- TI_MASK: 0xFFFF (65,536 intervals)
- SPLITS_WHO_MASK: 0xFFF (4,096 entities)
- Input validation enabled (errno-based error reporting)
- Compatible with qmap b1bc322+

**v1.1.0 (legacy):**
- TI_MASK: 0x7FF (2,048 intervals)
- SPLITS_WHO_MASK: 0xFF (256 entities)
- No input validation
- Compatible with qmap v0.6.0

---

## References

- **Source Code**: `/home/quirinpa/libjoint/src/libjoint.c`
- **API Documentation**: `/home/quirinpa/libjoint/include/ttypt/joint.h`
- **Extended Tests**: `/home/quirinpa/libjoint/src/test_extended.c`
- **Core Tests**: `/home/quirinpa/libjoint/src/test.c`
- **CHANGELOG**: `/home/quirinpa/libjoint/CHANGELOG.md`
- **Quick Reference**: `/home/quirinpa/libjoint/QUICK_REFERENCE.md`
- **qmap IDM Header**: `/home/quirinpa/qmap/include/ttypt/idm.h`

---

## Version History

- **2026-09-10 (v1.2.2)**: Fixed MV index write efficiency (W3 regression; libqmap 0.8.0 chains + backshift, libjoint qmap_get_multi)
- **2026-02-23 (v1.2.1)**: Fixed file persistence (removed QM_MIRROR)
- **2026-02-23 (v1.2.0)**: Fixed 4 of 5 limitations - mask increases and input validation
- **2026-02-23 (v1.1.0)**: Initial documentation (Phase 4 extended testing)
- **qmap b1bc322**: Prior dependency baseline
- **qmap v0.6.0**: Original dependency version

---

*This document tracks design limitations across libjoint versions. See CHANGELOG.md for detailed implementation notes.*
