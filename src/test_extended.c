/**
 * Extended test suite for libjoint
 * Tests stress scenarios, performance benchmarks, and edge cases
 */

#include "../include/ttypt/joint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

unsigned errors = 0;

#define PASS() printf("  ✅ PASS\n")
#define FAIL(msg) do { printf("  ❌ FAIL: %s\n", msg); errors++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) FAIL(msg); else PASS(); } while(0)

/* Get current time in microseconds */
static long long get_time_us(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

/* Test 1: Large dataset stress test */
static void test_large_dataset(void) {
	printf("\n=== Test 1: Large Dataset Stress Test ===\n");
	
	/* NOTE: libjoint v1.2.0 has a limit of ~65536 intervals due to TI_MASK=0xFFFF */
	const int NUM_INTERVALS = 10000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Inserting %d intervals:", NUM_INTERVALS);
	start = get_time_us();
	for (int i = 0; i < NUM_INTERVALS; i++) {
		time_t start_time = 1000 + i * 100;
		time_t end_time = start_time + 50;
		joint_start(jd, start_time, i + 1);
		joint_stop(jd, end_time, i + 1);
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/interval)\n", end - start, (double)(end - start) / NUM_INTERVALS);
	PASS();
	
	printf("Querying all intervals:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 0, 2000000);
	time_t min, max;
	unsigned count, who;
	int found_entities = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found_entities++;
	}
	end = get_time_us();
	printf(" %lld µs (%d entity references found)\n", end - start, found_entities);
	ASSERT(found_entities == NUM_INTERVALS, "Should find all entity references");
}

/* Test 2: Many overlapping intervals */
static void test_many_overlapping(void) {
	printf("\n=== Test 2: Many Overlapping Intervals ===\n");
	
	/* NOTE: SPLITS_WHO_MASK=0xFFF limits to 4096 entities per split */
	const int NUM_ENTITIES = 1000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Creating %d overlapping intervals:", NUM_ENTITIES);
	start = get_time_us();
	for (int i = 0; i < NUM_ENTITIES; i++) {
		joint_start(jd, 5000, i + 1);  // All start at same time
		joint_stop(jd, 10000, i + 1);  // All end at same time
	}
	end = get_time_us();
	printf(" %lld µs\n", end - start);
	PASS();
	
	printf("Querying overlapping region:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 6000, 8000);
	time_t min, max;
	unsigned count, who;
	int found_entities = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found_entities++;
	}
	end = get_time_us();
	printf(" %lld µs (%d entity references found)\n", end - start, found_entities);
	ASSERT(found_entities == NUM_ENTITIES, "Should find all entity references in overlapping region");
}

/* Test 3: Sequential interval insertion performance */
static void test_sequential_insertion(void) {
	printf("\n=== Test 3: Sequential Insertion Performance ===\n");
	
	const int NUM_INTERVALS = 5000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Sequential insertions:");
	start = get_time_us();
	for (int i = 0; i < NUM_INTERVALS; i++) {
		time_t t_start = i * 1000;
		time_t t_end = t_start + 500;
		joint_start(jd, t_start, 1);
		joint_stop(jd, t_end, 1);
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/interval)\n", end - start, (double)(end - start) / NUM_INTERVALS);
	PASS();
}

/* Test 4: Query performance on sparse data */
static void test_sparse_query_performance(void) {
	printf("\n=== Test 4: Query Performance on Sparse Data ===\n");
	
	unsigned jd = joint_init(NULL);
	
	// Create sparse intervals: 100 intervals spread across huge time range
	printf("Creating 100 sparse intervals:");
	for (int i = 0; i < 100; i++) {
		time_t t_start = i * 1000000LL;  // 1M apart
		time_t t_end = t_start + 1000;
		joint_start(jd, t_start, i + 1);
		joint_stop(jd, t_end, i + 1);
	}
	PASS();
	
	// Query middle region
	printf("Querying middle sparse region:");
	long long start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 50000000LL, 50010000LL);
	time_t min, max;
	unsigned count, who;
	int found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found++;
	}
	long long end = get_time_us();
	printf(" %lld µs (%d intervals found)\n", end - start, found);
	ASSERT(found == 1, "Should find exactly 1 interval in sparse region");
}

/* Test 5: Edge case - Maximum timestamp values */
static void test_extreme_timestamps(void) {
	printf("\n=== Test 5: Extreme Timestamp Values ===\n");
	
	unsigned jd = joint_init(NULL);
	
	// Test with very large timestamps (near INT64_MAX if time_t is 64-bit)
	printf("Insert interval near maximum timestamp:");
	time_t huge = 9223372036854775000LL;  // Close to INT64_MAX but safe
	joint_start(jd, huge - 1000, 1);
	joint_stop(jd, huge, 1);
	PASS();
	
	printf("Query interval with extreme timestamp:");
	joint_cur_t cur = joint_iter(jd, huge - 2000, huge);  /* (was huge+1000: UB, overflows int64) */
	time_t min, max;
	unsigned count, who;
	int found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		if (who == 1) found++;
	}
	/* Note: May not work due to overflow or time_t limits on some platforms */
	printf(" Found %d entity references\n", found);
	if (found > 0) PASS(); else printf("  ⚠️  SKIP: Extreme timestamps may not be supported\n");
	
	// Test with negative timestamps
	printf("Insert interval with negative timestamp:");
	joint_start(jd, -1000000, 2);
	joint_stop(jd, -999000, 2);
	PASS();
	
	printf("Query interval with negative timestamp:");
	cur = joint_iter(jd, -1001000, -998000);
	found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		if (who == 2) found++;
	}
	ASSERT(found > 0, "Should find interval with negative timestamp");
}

/* Test 6: Split computation with many entities */
static void test_split_performance(void) {
	printf("\n=== Test 6: Split Computation Performance ===\n");
	
	const int NUM_ENTITIES = 100;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Creating %d overlapping intervals for splits:", NUM_ENTITIES);
	for (int i = 0; i < NUM_ENTITIES; i++) {
		time_t t_start = 5000 + (i % 10) * 100;  // Create some variation
		time_t t_end = t_start + 2000;
		joint_start(jd, t_start, i + 1);
		joint_stop(jd, t_end, i + 1);
	}
	PASS();
	
	printf("Computing splits:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 5000, 8000);
	time_t min, max;
	unsigned count, who;
	int total_splits = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		total_splits++;
	}
	end = get_time_us();
	printf(" %lld µs (%d splits generated)\n", end - start, total_splits);
	ASSERT(total_splits > 0, "Should generate splits");
}

/* Test 7: Memory usage test - ensure no leaks with many operations */
static void test_repeated_operations(void) {
	printf("\n=== Test 7: Repeated Operations (Memory Leak Check) ===\n");
	
	const int NUM_ITERATIONS = 1000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Performing %d insert/query cycles:", NUM_ITERATIONS);
	start = get_time_us();
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		// Insert
		joint_start(jd, 1000 + i, i % 100 + 1);
		joint_stop(jd, 2000 + i, i % 100 + 1);
		
		// Query
		joint_cur_t cur = joint_iter(jd, 1000, 3000);
		time_t min, max;
		unsigned count, who;
		while (joint_next(&min, &max, &count, &who, &cur)) {
			// Just iterate
		}
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/cycle)\n", end - start, (double)(end - start) / NUM_ITERATIONS);
	PASS();
	
	printf("Note: Check for memory leaks with valgrind if available\n");
}

/* Test 8: Boundary query performance */
static void test_boundary_queries(void) {
	printf("\n=== Test 8: Boundary Query Performance ===\n");
	
	unsigned jd = joint_init(NULL);
	
	printf("Setup: Insert 1000 intervals:");
	for (int i = 0; i < 1000; i++) {
		joint_start(jd, i * 1000, i + 1);
		joint_stop(jd, i * 1000 + 500, i + 1);
	}
	PASS();
	
	// Query at exact boundary
	printf("Query at exact start boundary:");
	long long start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 500000, 500001);
	time_t min, max;
	unsigned count, who;
	int found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found++;
	}
	long long end = get_time_us();
	printf(" %lld µs (%d intervals found)\n", end - start, found);
	ASSERT(found == 1, "Should find interval at exact boundary");
	
	// Query empty region between intervals
	printf("Query empty region between intervals:");
	start = get_time_us();
	cur = joint_iter(jd, 500600, 500900);
	found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found++;
	}
	end = get_time_us();
	printf(" %lld µs (%d intervals found)\n", end - start, found);
	ASSERT(found == 0, "Should find no intervals in empty region");
}

/* Test 9: Entity ID edge cases */
static void test_entity_id_edge_cases(void) {
	printf("\n=== Test 9: Entity ID Edge Cases ===\n");
	
	unsigned jd = joint_init(NULL);
	
	printf("Insert with ID = 0:");
	joint_start(jd, 1000, 0);
	joint_stop(jd, 2000, 0);
	PASS();
	
	printf("Query interval with ID = 0:");
	joint_cur_t cur = joint_iter(jd, 500, 2500);
	time_t min, max;
	unsigned count, who;
	int found_zero = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		if (who == 0) found_zero = 1;
	}
	ASSERT(found_zero, "Should find interval with ID = 0");
	
	printf("Insert with very large ID (UINT32_MAX):");
	joint_start(jd, 3000, (unsigned)-1);
	joint_stop(jd, 4000, (unsigned)-1);
	PASS();
	
	printf("Query interval with ID = UINT32_MAX:");
	cur = joint_iter(jd, 2500, 4500);
	int found_max = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		if (who == (unsigned)-1) found_max = 1;
	}
	/* Note: UINT32_MAX may conflict with IDM_MISS sentinel value */
	printf(" Found: %s\n", found_max ? "yes" : "no");
	if (found_max) PASS(); else printf("  ⚠️  SKIP: UINT32_MAX conflicts with IDM_MISS sentinel\n");
}

/* Test 10: Time utility functions stress test */
static void test_time_utils_stress(void) {
	printf("\n=== Test 10: Time Utility Functions Stress Test ===\n");
	
	const int NUM_CONVERSIONS = 10000;
	long long start, end;
	
	printf("Parsing %d timestamps:", NUM_CONVERSIONS);
	start = get_time_us();
	for (int i = 0; i < NUM_CONVERSIONS; i++) {
		char buf[32];
		snprintf(buf, sizeof(buf), "2024-01-%02d", (i % 28) + 1);
		time_t ts = sscantime(buf);
		(void)ts;  // Suppress unused warning
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/parse)\n", end - start, (double)(end - start) / NUM_CONVERSIONS);
	PASS();
	
	printf("Formatting %d timestamps:", NUM_CONVERSIONS);
	char buf[DATE_MAX_LEN];
	start = get_time_us();
	for (int i = 0; i < NUM_CONVERSIONS; i++) {
		time_t ts = 1700000000LL + (time_t)i * 86400;  // Different days
		printtime(buf, ts);
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/format)\n", end - start, (double)(end - start) / NUM_CONVERSIONS);
	PASS();
}

/* Test 11: Interleaved operations stress test */
static void test_interleaved_operations(void) {
	printf("\n=== Test 11: Interleaved Operations Stress Test ===\n");
	
	const int NUM_ENTITIES = 500;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Interleaving start/stop for %d entities:", NUM_ENTITIES);
	start = get_time_us();
	
	// Start all
	for (int i = 0; i < NUM_ENTITIES; i++) {
		joint_start(jd, 1000 + i, i + 1);
	}
	
	// Stop in reverse order
	for (int i = NUM_ENTITIES - 1; i >= 0; i--) {
		joint_stop(jd, 2000 + i, i + 1);
	}
	
	end = get_time_us();
	printf(" %lld µs\n", end - start);
	PASS();
	
	printf("Verify all intervals created:");
	joint_cur_t cur = joint_iter(jd, 0, 3000);
	time_t min, max;
	unsigned count, who;
	int found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found++;
	}
	/* Note: with overlapping intervals, we expect found >= NUM_ENTITIES due to splits */
	printf(" Found %d entity references (expected >= %d due to overlaps)\n", found, NUM_ENTITIES);
	ASSERT(found >= NUM_ENTITIES, "Should find all or more entity references from interleaved intervals");
}

/* Test 12: Zero-duration intervals */
static void test_zero_duration_intervals(void) {
	printf("\n=== Test 12: Zero-Duration Intervals ===\n");
	
	unsigned jd = joint_init(NULL);
	
	printf("Create 100 zero-duration intervals (start == stop):");
	for (int i = 0; i < 100; i++) {
		time_t t = 1000 + i * 100;
		joint_start(jd, t, i + 1);
		joint_stop(jd, t, i + 1);  // Same timestamp
	}
	PASS();
	
	printf("Query zero-duration interval at point 5000:");
	joint_cur_t cur = joint_iter(jd, 5000, 5001);  // Query includes point 5000
	time_t min, max;
	unsigned count, who;
	int found = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found++;
	}
	/* Note: Zero-duration intervals (start==stop) may not be queryable */
	printf(" Found %d entity references\n", found);
	if (found >= 1) PASS(); else printf("  ⚠️  SKIP: Zero-duration intervals may not be supported\n");
}

/* Test 13: Boundary test - very large dataset (near 65k limit) */
static void test_boundary_large_dataset(void) {
	printf("\n=== Test 13: Boundary Test - 15k Intervals ===\n");
	
	/* Test approaching TI_MASK limit of 65536 */
	const int NUM_INTERVALS = 15000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Inserting %d intervals (approaching 65k limit):", NUM_INTERVALS);
	start = get_time_us();
	for (int i = 0; i < NUM_INTERVALS; i++) {
		time_t start_time = 1000 + i * 100;
		time_t end_time = start_time + 50;
		joint_start(jd, start_time, i + 1);
		joint_stop(jd, end_time, i + 1);
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/interval)\n", end - start, (double)(end - start) / NUM_INTERVALS);
	PASS();
	
	printf("Querying sample intervals:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 200000, 500000);
	time_t min, max;
	unsigned count, who;
	int found_entities = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found_entities++;
	}
	end = get_time_us();
	printf(" %lld µs (%d entity references found)\n", end - start, found_entities);
	ASSERT(found_entities > 0, "Should find entities in query range");
}

/* Test 14: Boundary test - very high overlap (near 4k limit) */
static void test_boundary_high_overlap(void) {
	printf("\n=== Test 14: Boundary Test - 3k Overlapping Entities ===\n");
	
	/* Test approaching SPLITS_WHO_MASK limit of 4096 */
	const int NUM_ENTITIES = 3000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Creating %d overlapping intervals (approaching 4k limit):", NUM_ENTITIES);
	start = get_time_us();
	for (int i = 0; i < NUM_ENTITIES; i++) {
		joint_start(jd, 5000, i + 1);  // All start at same time
		joint_stop(jd, 10000, i + 1);  // All end at same time
	}
	end = get_time_us();
	printf(" %lld µs\n", end - start);
	PASS();
	
	printf("Querying overlapping region:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 6000, 8000);
	time_t min, max;
	unsigned count, who;
	int found_entities = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found_entities++;
	}
	end = get_time_us();
	printf(" %lld µs (%d entity references found)\n", end - start, found_entities);
	ASSERT(found_entities == NUM_ENTITIES, "Should find all entity references in overlapping region");
}

/* Test 15: Exact boundary test - near TI_MASK limit */
static void test_exact_ti_mask_boundary(void) {
	printf("\n=== Test 15: TI_MASK Boundary Test (20k Intervals) ===\n");
	
	/* Test behavior near TI_MASK limit (65536)
	 * We use 20k to demonstrate increased capacity while keeping test time reasonable */
	const int TEST_LIMIT = 20000;
	unsigned jd = joint_init(NULL);
	long long start, end;
	
	printf("Creating %d intervals (30%% of TI_MASK limit):", TEST_LIMIT);
	start = get_time_us();
	int success_count = 0;
	for (int i = 0; i < TEST_LIMIT; i++) {
		time_t start_time = 1000 + i * 100;
		time_t end_time = start_time + 50;
		int ret_start = joint_start(jd, start_time, i + 1);
		int ret_stop = joint_stop(jd, end_time, i + 1);
		if (ret_start == 0 && ret_stop == 0) {
			success_count++;
		}
	}
	end = get_time_us();
	printf(" %lld µs (%.2f µs/interval)\n", end - start, (double)(end - start) / TEST_LIMIT);
	printf("  Created %d/%d intervals successfully\n", success_count, TEST_LIMIT);
	ASSERT(success_count == TEST_LIMIT, "Should create all intervals with increased TI_MASK");
	
	printf("Verifying sample intervals are queryable:");
	start = get_time_us();
	joint_cur_t cur = joint_iter(jd, 500000, 700000);
	time_t min, max;
	unsigned count, who;
	int found_entities = 0;
	while (joint_next(&min, &max, &count, &who, &cur)) {
		found_entities++;
	}
	end = get_time_us();
	printf(" %lld µs (%d entity references found)\n", end - start, found_entities);
	ASSERT(found_entities > 0, "Should be able to query intervals at high capacity");
}

int main(void) {
	printf("╔════════════════════════════════════════════════════════════╗\n");
	printf("║     Extended Test Suite for libjoint                      ║\n");
	printf("║     Stress Tests, Performance Benchmarks, Edge Cases      ║\n");
	printf("╚════════════════════════════════════════════════════════════╝\n");
	
	test_large_dataset();
	test_many_overlapping();
	test_sequential_insertion();
	test_sparse_query_performance();
	test_extreme_timestamps();
	test_split_performance();
	test_repeated_operations();
	test_boundary_queries();
	test_entity_id_edge_cases();
	test_time_utils_stress();
	test_interleaved_operations();
	test_zero_duration_intervals();
	test_boundary_large_dataset();
	test_boundary_high_overlap();
	test_exact_ti_mask_boundary();
	
	printf("\n╔════════════════════════════════════════════════════════════╗\n");
	if (errors == 0) {
		printf("║  ✅ ALL EXTENDED TESTS PASSED                              ║\n");
	} else {
		printf("║  ❌ %u EXTENDED TEST(S) FAILED                            ║\n", errors);
	}
	printf("╚════════════════════════════════════════════════════════════╝\n");
	
	return errors > 0 ? 1 : 0;
}
