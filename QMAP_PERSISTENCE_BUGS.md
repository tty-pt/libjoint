# qmap Persistence Bugs - Investigation Summary

## Date
February 23, 2026

## Context
While implementing Phase 3 of libjoint v1.1.0 testing (persistence tests), we discovered critical bugs in qmap v0.6.0's file persistence functionality.

## Bugs Discovered

### Bug 1: Multiple Databases Per File with QM_MIRROR Fails
**Severity**: Critical  
**Status**: Confirmed

**Description**:  
When multiple qmap databases are created on the same file using `QM_MIRROR`, data persistence fails completely. Data is not loaded back after closing and reopening.

**Reproduction**:
```c
// Create 3 databases on same file
hd1 = qmap_open("/tmp/multi.db", "db1", QM_U32, QM_U32, 0xFF, QM_MIRROR);
hd2 = qmap_open("/tmp/multi.db", "db2", QM_U32, QM_U32, 0xFF, QM_MIRROR);
hd3 = qmap_open("/tmp/multi.db", "db3", QM_U32, QM_U32, 0xFF, QM_MIRROR);

// Insert data
qmap_put(hd1, &key, &val);

// Save and close
qmap_save();
qmap_close(hd1);
qmap_close(hd2);
qmap_close(hd3);

// Reopen
hd1 = qmap_open("/tmp/multi.db", "db1", QM_U32, QM_U32, 0xFF, QM_MIRROR);

// Query - DATA NOT FOUND!
result = qmap_get(hd1, &key);  // Returns NULL
```

**Expected**: Data should persist and be loaded back.  
**Actual**: `qmap_get()` returns NULL - data not found.

**Workaround**: Use separate files for each database instead of multiple databases per file.

**Test Files**:
- `/tmp/multi_db_test.c` - Reproduces with custom types
- `/tmp/multi_db_test2.c` - Reproduces with built-in types (QM_U32)

---

### Bug 2: Process Exit Crash with QM_MIRROR and Custom Types
**Severity**: Critical  
**Status**: Confirmed

**Description**:  
When using `QM_MIRROR` with custom registered types and file-backed databases, the program crashes during exit cleanup with "free(): invalid pointer" error.

**Reproduction**:
```c
// Register custom type
uint32_t qm_struct = qmap_reg(sizeof(struct my_struct));

// Create file-backed database with QM_MIRROR
hd = qmap_open("/tmp/test.db", "db", qm_struct, qm_struct, 0xFF, QM_MIRROR);

// Insert data and save
qmap_put(hd, &key_struct, &val_struct);
qmap_save();

// DON'T close explicitly - let process exit
// CRASH: free(): invalid pointer
```

**Expected**: Clean exit with automatic save via destructor.  
**Actual**: Segfault/abort with memory corruption error during qmap's destructor.

**Workaround**: Explicitly close all file-backed databases before program exit, or avoid QM_MIRROR with custom types.

---

## Impact on libjoint

### Original Design
libjoint v1.1.0 was designed to use 3 qmap databases per interval tree:
- `ti`: Primary database (interval -> interval)
- `max`: Secondary index sorted by max timestamp
- `id`: Secondary index sorted by entity ID

All three would share the same file for persistence.

### Workaround Attempted
Changed to use separate files:
- `<fname>-ti` for primary database
- `<fname>-max` for max index
- `<fname>-id` for id index

**Result**: Still crashes due to Bug #2.

### Current Solution
**Persistence disabled** in libjoint v1.1.0:
1. Added `joint_close()` function (for future use)
2. Implemented persistence test infrastructure (Category 7 tests)
3. **Disabled** Category 7 tests with note: "SKIPPED: Persistence tests disabled due to qmap bug"
4. File persistence marked as **NOT WORKING** until qmap bugs are fixed

---

## Files Modified (libjoint)

### Core Changes
-  `/home/quirinpa/libjoint/src/libjoint.c`:
  - Added `joint_close()` function (lines 619-630)
  - Fixed `sscantime()` errno bug (line 87)
  - Fixed `printtime()` missing return statements (lines 106, 111)
  - Modified `tidbs_init()` to use QM_MIRROR flag (line 165)

- `/home/quirinpa/libjoint/include/ttypt/joint.h`:
  - Added `joint_close()` documentation (lines 88-102)

### Test Infrastructure
- `/home/quirinpa/libjoint/src/test.c`:
  - Added Category 6 tests (time utilities) - 6 tests
  - Added Category 7 tests (persistence) - 5 tests (DISABLED)
  - Updated test runner to skip persistence tests

---

## Recommendations

### For qmap Maintainers
1. **Fix Bug #1**: Investigate `qmap_load_file()` and `_qmap_load()` to understand why multiple databases per file fail to load
2. **Fix Bug #2**: Investigate memory management in qmap destructor when QM_MIRROR is used with custom types
3. **Add Tests**: qmap's `test_extended.c` only tests built-in types (QM_U32, QM_STR); add tests with custom registered types

### For libjoint
1. **Short-term**: Keep persistence disabled until qmap bugs are fixed
2. **Medium-term**: Consider alternative approaches:
   - Use built-in types only (requires redesigning data structures)
   - Implement custom persistence layer (bypassing qmap's)
   - Switch to different storage backend (e.g., SQLite, LMDB)
3. **Long-term**: Contribute fixes to qmap or fork for libjoint-specific needs

---

## Test Evidence

All test programs are in `/tmp/`:
- `qmap_test.c` - Single DB, no QM_MIRROR: ❌ FAILS
- `qmap_test2.c` - Single DB, with QM_MIRROR: ✅ WORKS
- `multi_db_test.c` - Multi DB, custom types: ❌ FAILS (Bug #1)
- `multi_db_test2.c` - Multi DB, built-in types: ❌ FAILS (Bug #1)
- `single_db_test.c` - Single DB per file, built-in types: ✅ WORKS
- `debug_persist*.c` - libjoint persistence tests: ❌ FAIL (both bugs)
- `append_test.c` - Demonstrates data corruption on reopen

---

## Conclusion

qmap v0.6.0 has fundamental issues with:
1. Multiple databases per file when using QM_MIRROR
2. Custom types with QM_MIRROR causing memory corruption

These bugs block file persistence in libjoint until resolved. Phase 3 partially completed:
- ✅ Category 6 (Time Utilities): 6 tests passing
- ⏸️ Category 7 (Persistence): 5 tests implemented but disabled

**Total Phase 3 contribution**: 50 tests (44 from Phases 1-2 + 6 from Category 6)

---

## Update: February 23, 2026 - Re-tested with qmap b1bc322

### Background
During libjoint v1.2.0 development, we discovered that qmap had a potentially relevant fix:
- **Commit df5a7ac**: "Fix two major design gotchas: file loading and pointer invalidation"
- This commit claimed to remove QM_MIRROR requirement for file loading
- Current qmap version: **b1bc322** (includes df5a7ac merged into main)

### Test Results
Re-enabled all 5 Category 7 persistence tests to check if qmap fixes resolved the issues.

**Result**: **FAILED** - Segmentation fault on test execution

**Command**:
```bash
LD_LIBRARY_PATH=/home/quirinpa/libjoint/lib:/usr/lib ./bin/test
```

**Output**:
```
Segmentation fault (core dumped)
```

### Conclusion
The qmap persistence bugs **persist as of version b1bc322** (2026-02-23).

Despite the df5a7ac fix claiming to address file loading issues, the segmentation fault indicates:
1. The bugs are still present, OR
2. The fix introduced new issues, OR  
3. libjoint's usage pattern exposes a different bug

**Action Taken**: Category 7 persistence tests remain disabled with updated comment:
```c
/* DISABLED: Persistence tests cause segmentation fault with qmap b1bc322
 * Tested 2026-02-23 with qmap version b1bc322 (includes df5a7ac fix)
 * Bugs still present - keeping tests disabled pending upstream qmap fixes
 * See QMAP_PERSISTENCE_BUGS.md for details
```

**Recommendation**: Continue monitoring qmap development. May need to:
- File detailed bug report with qmap maintainers
- Investigate if QM_MIRROR removal (per df5a7ac) requires code changes in libjoint
- Consider alternative persistence strategies if qmap issues persist

---

## UPDATE: February 23, 2026 (libjoint v1.2.1)

### Resolution: PERSISTENCE NOW WORKS ✅

**Solution Found**: The issue was NOT a bug in qmap that needed fixing, but rather a **design change** in qmap v0.7.0+.

### What Changed
qmap v0.7.0+ (specifically commit df5a7ac) made a fundamental change:
- **Before**: QM_MIRROR was **required** for file persistence
- **After**: QM_MIRROR is **optional**; file persistence works without it
- QM_MIRROR is now only needed for bidirectional lookups

### libjoint Fix Applied
Changed `src/libjoint.c:166`:
```c
// OLD (v1.2.0):
uint32_t flags = fname ? QM_MIRROR : 0;

// NEW (v1.2.1):
uint32_t flags = 0;  // QM_MIRROR optional in qmap v0.7.0+
```

### Test Results (v1.2.1)
All 5 persistence tests now pass:
```
=== Category 7: Persistence ===
test_persist_save_load ✅ 
test_persist_multiple_intervals ✅ 
test_persist_empty_database ✅ 
test_persist_append ✅ 
test_persist_large_dataset ✅ 
```

### Why This Works
- libjoint doesn't use `qmap_assoc()` (bidirectional lookups)
- libjoint only needs basic file persistence, not QM_MIRROR features
- qmap v0.7.0+ automatically loads persisted data without QM_MIRROR flag

### Conclusion
The qmap bugs documented above are **specific to QM_MIRROR usage** and do not affect libjoint since libjoint doesn't need QM_MIRROR. By removing the QM_MIRROR flag, file persistence now works perfectly.
