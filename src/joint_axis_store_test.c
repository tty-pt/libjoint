/* 2A-3 joint_erase + rec_axis_store/unstore/readback contract tests.
 * Self-checking (CHECK + summary, no expects.txt diff), like
 * stoma_axis_store_test.c. Covers the native erase differential
 * (index -> erase -> zero residual intervals, neighbors unaffected)
 * and the four-form adapter grammar incl. exact-duplicate no-ops. */

#include "../include/ttypt/joint.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int total = 0;
static int failures = 0;

#define CHECK(cond, name) do { \
	total++; \
	if (!(cond)) { \
		printf("FAIL: %s (line %d)\n", name, __LINE__); \
		failures++; \
	} \
} while (0)

/* presence of `id` at [when, when+1) via the public iterator */
static int present(unsigned jd, time_t when, unsigned id)
{
	time_t a, b;
	unsigned count, who;
	joint_cur_t c = joint_iter(jd, when, when + 1);

	while (joint_next(&a, &b, &count, &who, &c))
		if (who == id)
			return 1;
	return 0;
}

/* parse a valid literal (public sscantime abort on garbage is fine here) */
static time_t D(const char *s)
{
	char buf[64];
	strcpy(buf, s);
	return sscantime(buf);
}

/* number of NUL-separated entries in a readback blob */
static size_t nul_count(const char *blob, size_t n)
{
	size_t c = 0;
	for (size_t i = 0; i < n; i++)
		if (blob[i] == '\0')
			c++;
	return c;
}

unsigned jd;
void *ctx;

static void setup(void)
{
	/* Burn handle 0: a jd of 0 widens to a NULL ctx pointer, which the
	 * locked contract rejects — so the shared adapter store must live
	 * on a nonzero handle (documented handle-0/NULL collision). */
	(void)joint_init(NULL);
	jd = joint_init(NULL);
	ctx = (void *)(uintptr_t)jd;
}

/* ---- native joint_erase ---- */

static void erase_all_removes_present(void)
{
	unsigned j = joint_init(NULL);
	joint_start(j, 100, 1);
	joint_stop(j, 200, 1);
	joint_start(j, 300, 1);
	joint_stop(j, 400, 1);
	CHECK(present(j, 150, 1), "present in first interval");
	CHECK(present(j, 350, 1), "present in second interval");

	CHECK(joint_erase(j, 1) == 0, "erase returns 0");
	CHECK(!present(j, 150, 1), "gone from first interval");
	CHECK(!present(j, 350, 1), "gone from second interval");
	CHECK(!present(j, 50, 1), "gone early");
	CHECK(!present(j, 450, 1), "gone late");
}

static void erase_open_interval(void)
{
	unsigned j = joint_init(NULL);
	joint_start(j, 100, 9);
	CHECK(present(j, 200, 9), "open interval present");
	CHECK(joint_erase(j, 9) == 0, "erase open interval");
	CHECK(!present(j, 200, 9), "open interval gone");
}

static void erase_absent_idempotent(void)
{
	unsigned j = joint_init(NULL);
	CHECK(joint_erase(j, 424242) == 0, "absent -> 0");
	CHECK(joint_erase(j, 424242) == 0, "absent again -> 0");
}

static void erase_uint32_max(void)
{
	unsigned j = joint_init(NULL);
	errno = 0;
	CHECK(joint_erase(j, UINT32_MAX) == -1, "UINT32_MAX -> -1");
	CHECK(errno == EINVAL, "UINT32_MAX errno EINVAL");
}

static void erase_neighbors_unaffected(void)
{
	unsigned j = joint_init(NULL);
	joint_start(j, 100, 1);
	joint_stop(j, 200, 1);
	joint_start(j, 500, 2);
	joint_stop(j, 600, 2);
	CHECK(joint_erase(j, 1) == 0, "erase id 1");
	CHECK(!present(j, 150, 1), "id 1 gone");
	CHECK(present(j, 550, 2), "id 2 unaffected");
}

static void erase_then_restart(void)
{
	unsigned j = joint_init(NULL);
	joint_start(j, 100, 5);
	CHECK(joint_erase(j, 5) == 0, "erase");
	joint_start(j, 700, 5);
	CHECK(present(j, 750, 5), "restarted fresh timeline present");
	CHECK(joint_erase(j, 5) == 0, "erase again");
}

static void erase_double_backfill_clean(void)
{
	/* Native double backfill duplicates the id index; erase must mop it. */
	unsigned j = joint_init(NULL);
	time_t b = 123456;
	joint_stop(j, b, 3);
	joint_stop(j, b, 3);
	CHECK(joint_erase(j, 3) == 0, "erase after double backfill");

	rec_set_t *s = rec_set_new();
	CHECK(rec_axis_fill_interval(j, 0, b + 100, s) == 0, "fill ok");
	CHECK(rec_set_count(s) == 0, "no phantom refs after erase");
	rec_set_free(s);

	char *blob = (char *)0x1;
	size_t n = 1;
	void *jctx = (void *)(uintptr_t)j;
	CHECK(rec_axis_readback(jctx, 3, &blob, &n) == 0, "readback ok");
	CHECK(blob == NULL && n == 0, "id index fully clean (no phantom)");
	free(blob);
}

/* ---- adapter store/unstore/readback ---- */

static void store_open_readback(void)
{
	time_t a = D("2024-01-01");
	char expect[DATE_MAX_LEN];
	printtime(expect, a);
	CHECK(rec_axis_store(ctx, NULL, 11, "2024-01-01") == 0, "open store");
	CHECK(present(jd, a, 11), "present at start");
	CHECK(present(jd, D("2024-12-31"), 11), "present late (open)");

	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 11, &blob, &n) == 0, "readback open");
	CHECK(blob && strcmp(blob, expect) == 0, "readback == open entry");
	CHECK(n == strlen(expect) + 1, "n_out correct");
	CHECK(nul_count(blob, n) == 1, "one entry");
	free(blob);
}

static void store_atomic_readback(void)
{
	time_t a = D("2024-01-01");
	time_t b = D("2024-06-01");
	char ea[DATE_MAX_LEN], eb[DATE_MAX_LEN], expect[2 * DATE_MAX_LEN + 2];
	printtime(ea, a);
	printtime(eb, b);
	snprintf(expect, sizeof expect, "%s,%s", ea, eb);

	CHECK(rec_axis_store(ctx, NULL, 12, "2024-01-01,2024-06-01") == 0,
	      "atomic store");
	CHECK(present(jd, a, 12), "present at start");
	CHECK(present(jd, D("2024-03-15"), 12), "present inside");
	CHECK(!present(jd, b, 12), "absent at end (exclusive)");

	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 12, &blob, &n) == 0, "readback atomic");
	CHECK(blob && strcmp(blob, expect) == 0, "readback == atomic entry");
	free(blob);
}

static void store_mm_leading_date(void)
{
	time_t a = D("2024-01-01");
	CHECK(rec_axis_store(ctx, NULL, 13, "2024-01-01:hello world") == 0,
	      "mm leading-date store");
	CHECK(present(jd, D("2024-03-01"), 13), "open interval from prefix");

	char *blob = NULL;
	size_t n = 0;
	char expect[DATE_MAX_LEN];
	printtime(expect, a);
	CHECK(rec_axis_readback(ctx, 13, &blob, &n) == 0, "readback mm");
	CHECK(blob && strcmp(blob, expect) == 0, "readback == open entry");
	free(blob);
}

static void store_epoch(void)
{
	time_t ts = D("1704067200");
	CHECK(rec_axis_store(ctx, NULL, 14, "1704067200") == 0, "epoch store");
	CHECK(present(jd, ts, 14), "present at epoch instant");
}

static void store_backfill_readback(void)
{
	time_t b = D("2024-06-01");
	char eb[DATE_MAX_LEN], expect[DATE_MAX_LEN + 2];
	printtime(eb, b);
	expect[0] = ',';
	strcpy(expect + 1, eb);

	CHECK(rec_axis_store(ctx, NULL, 15, ",2024-06-01") == 0, "backfill store");
	CHECK(present(jd, D("2020-01-01"), 15), "present before (backfilled)");

	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 15, &blob, &n) == 0, "readback backfill");
	CHECK(blob && strcmp(blob, expect) == 0, "readback == backfill entry");
	free(blob);
}

static void store_duplicate_open_noop(void)
{
	CHECK(rec_axis_store(ctx, NULL, 16, "2024-01-01") == 0, "open 1st");
	CHECK(rec_axis_store(ctx, NULL, 16, "2024-01-01") == 0, "open 2nd no-op");
	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 16, &blob, &n) == 0, "readback dup open");
	CHECK(blob && nul_count(blob, n) == 1, "still exactly one entry");
	free(blob);
}

static void store_duplicate_atomic_noop(void)
{
	CHECK(rec_axis_store(ctx, NULL, 17, "2024-01-01,2024-06-01") == 0,
	      "atomic 1st");
	CHECK(rec_axis_store(ctx, NULL, 17, "2024-01-01,2024-06-01") == 0,
	      "atomic 2nd no-op");
	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 17, &blob, &n) == 0, "readback dup atomic");
	CHECK(blob && nul_count(blob, n) == 1, "still exactly one entry");
	free(blob);
}

static void store_duplicate_backfill_noop(void)
{
	CHECK(rec_axis_store(ctx, NULL, 18, ",2024-06-01") == 0, "backfill 1st");
	CHECK(rec_axis_store(ctx, NULL, 18, ",2024-06-01") == 0, "backfill 2nd no-op");
	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 18, &blob, &n) == 0, "readback dup backfill");
	CHECK(blob && nul_count(blob, n) == 1, "no duplicate [-inf,B) entry");
	free(blob);
}

static void store_restate_different_start_disjoint(void)
{
	CHECK(rec_axis_store(ctx, NULL, 19, "2024-01-01,2024-02-01") == 0,
	      "first interval");
	CHECK(rec_axis_store(ctx, NULL, 19, "2024-03-01,2024-04-01") == 0,
	      "second disjoint interval");
	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 19, &blob, &n) == 0, "readback multi");
	CHECK(blob && nul_count(blob, n) == 2, "two disjoint intervals kept");
	free(blob);
}

static void store_rejects_b_le_a(void)
{
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 20, "2024-06-01,2024-01-01") == -1,
	      "B<=A rejected");
	CHECK(errno == EINVAL, "B<=A errno EINVAL");
}

static void store_rejects_garbage(void)
{
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 21, "hello:world") == -1, "garbage rejected");
	CHECK(errno == EINVAL, "garbage errno EINVAL");
}

static void store_rejects_bad_comma_shapes(void)
{
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 22, "2024-01-01X,2024-06-01") == -1,
	      "left not a clean date");
	CHECK(errno == EINVAL, "bad left errno EINVAL");
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 22, "2024-01-01,") == -1,
	      "empty right");
	CHECK(errno == EINVAL, "empty right errno EINVAL");
}

static void store_rejects_null_and_empty(void)
{
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 23, NULL) == -1, "NULL value");
	CHECK(errno == EINVAL, "NULL value errno EINVAL");
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, 23, "") == -1, "empty value");
	CHECK(errno == EINVAL, "empty value errno EINVAL");
}

static void store_null_ctx(void)
{
	errno = 0;
	CHECK(rec_axis_store(NULL, NULL, 24, "2024-01-01") == -1, "NULL ctx");
	CHECK(errno == EINVAL, "NULL ctx errno EINVAL");
}

static void store_uint32_max(void)
{
	errno = 0;
	CHECK(rec_axis_store(ctx, NULL, UINT32_MAX, "2024-01-01") == -1,
	      "ref UINT32_MAX rejected");
	CHECK(errno == EINVAL, "ref UINT32_MAX errno EINVAL");
}

static void unstore_contract(void)
{
	CHECK(rec_axis_store(ctx, NULL, 25, "2024-01-01") == 0, "store for unstore");
	CHECK(rec_axis_unstore(ctx, 25) == 0, "unstore removes");
	CHECK(!present(jd, D("2024-06-01"), 25), "gone after unstore");
	CHECK(rec_axis_unstore(ctx, 25) == 0, "unstore twice -> 0");
	CHECK(rec_axis_unstore(ctx, 31337) == 0, "unstore absent -> 0");

	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 25, &blob, &n) == 0, "readback after unstore");
	CHECK(blob == NULL && n == 0, "absent readback NULL/0/0");
	free(blob);

	errno = 0;
	CHECK(rec_axis_unstore(NULL, 25) == -1, "unstore NULL ctx");
	CHECK(errno == EINVAL, "unstore NULL ctx errno EINVAL");
}

static void unstore_neighbors_unaffected(void)
{
	CHECK(rec_axis_store(ctx, NULL, 26, "2024-01-01") == 0, "store 26");
	CHECK(rec_axis_store(ctx, NULL, 27, "2024-01-01") == 0, "store 27");
	CHECK(rec_axis_unstore(ctx, 26) == 0, "unstore 26");
	CHECK(present(jd, D("2024-06-01"), 27), "ref 27 survives");
	CHECK(!present(jd, D("2024-06-01"), 26), "ref 26 gone");
}

static void readback_null_args(void)
{
	char *blob = NULL;
	size_t n = 0;
	errno = 0;
	CHECK(rec_axis_readback(NULL, 28, &blob, &n) == -1, "readback NULL ctx");
	CHECK(errno == EINVAL, "NULL ctx errno EINVAL");
	errno = 0;
	CHECK(rec_axis_readback(ctx, 28, NULL, &n) == -1, "readback NULL blob");
	CHECK(errno == EINVAL, "NULL blob errno EINVAL");
	errno = 0;
	CHECK(rec_axis_readback(ctx, 28, &blob, NULL) == -1, "readback NULL n");
	CHECK(errno == EINVAL, "NULL n errno EINVAL");
}

static void readback_roundtrip(void)
{
	CHECK(rec_axis_store(ctx, NULL, 29, "2024-01-01,2024-06-01") == 0, "store");
	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, 29, &blob, &n) == 0, "readback");
	char *round = malloc(n);
	memcpy(round, blob, n);
	free(blob);
	CHECK(rec_axis_store(ctx, NULL, 29, round) == 0,
	      "store of readback (exact restate no-op)");
	free(round);
	CHECK(rec_axis_readback(ctx, 29, &blob, &n) == 0, "readback again");
	CHECK(blob && nul_count(blob, n) == 1, "round-trip keeps single entry");
	free(blob);
}

static void readback_multi_native_built(void)
{
	/* Adapters refuse overlapping; build two disjoint intervals natively. */
	unsigned r = 30;
	unsigned j = jd;
	time_t a = D("2024-01-01"), b = D("2024-02-01");
	time_t c = D("2024-03-01"), d = D("2024-04-01");
	joint_start(j, a, r);
	joint_stop(j, b, r);
	joint_start(j, c, r);
	joint_stop(j, d, r);

	char ea[DATE_MAX_LEN], eb_str[DATE_MAX_LEN];
	char ec[DATE_MAX_LEN], ed_str[DATE_MAX_LEN];
	printtime(ea, a);
	printtime(eb_str, b);
	printtime(ec, c);
	printtime(ed_str, d);
	char entry1[2 * DATE_MAX_LEN + 2], entry2[2 * DATE_MAX_LEN + 2];
	snprintf(entry1, sizeof entry1, "%s,%s", ea, eb_str);
	snprintf(entry2, sizeof entry2, "%s,%s", ec, ed_str);
	size_t n1 = strlen(entry1), n2 = strlen(entry2);

	char *blob = NULL;
	size_t n = 0;
	CHECK(rec_axis_readback(ctx, r, &blob, &n) == 0, "readback multi");
	CHECK(blob != NULL, "blob non-null");
	if (blob) {
		CHECK(n == n1 + 1 + n2 + 1,
		      "blob length matches two NUL-joined entries");
		CHECK(memcmp(blob, entry1, n1 + 1) == 0, "entry1 'A,B' correct");
		CHECK(memcmp(blob + n1 + 1, entry2, n2 + 1) == 0,
		      "entry2 'C,D' correct");
		free(blob);
	}
}

static void fill_integration(void)
{
	/* Dedicated joint: the shared store holds many 2024 intervals. */
	unsigned j = joint_init(NULL);
	void *c = (void *)(uintptr_t)j;
	rec_set_t *s = rec_set_new();
	time_t a = D("2024-01-01"), b = D("2024-06-01");
	CHECK(rec_axis_store(c, NULL, 31, "2024-01-01,2024-06-01") == 0, "store");
	CHECK(rec_axis_fill_interval(j, a, b, s) == 0, "fill over interval");
	CHECK(rec_set_count(s) == 1 && rec_set_at(s)[0] == 31,
	      "fill finds the ref");
	rec_set_free(s);

	CHECK(rec_axis_unstore(c, 31) == 0, "unstore");
	s = rec_set_new();
	CHECK(rec_axis_fill_interval(j, a, b, s) == 0, "fill after unstore");
	CHECK(rec_set_count(s) == 0, "fill empty after unstore");
	rec_set_free(s);
}

int main(void)
{
	setup();

	printf("=== Category A: native joint_erase ===\n");
	erase_all_removes_present();
	erase_open_interval();
	erase_absent_idempotent();
	erase_uint32_max();
	erase_neighbors_unaffected();
	erase_then_restart();
	erase_double_backfill_clean();

	printf("=== Category B: adapter store/unstore/readback ===\n");
	store_open_readback();
	store_atomic_readback();
	store_mm_leading_date();
	store_epoch();
	store_backfill_readback();
	store_duplicate_open_noop();
	store_duplicate_atomic_noop();
	store_duplicate_backfill_noop();
	store_restate_different_start_disjoint();
	store_rejects_b_le_a();
	store_rejects_garbage();
	store_rejects_bad_comma_shapes();
	store_rejects_null_and_empty();
	store_null_ctx();
	store_uint32_max();
	unstore_contract();
	unstore_neighbors_unaffected();
	readback_null_args();
	readback_roundtrip();
	readback_multi_native_built();
	fill_integration();

	printf("\nResults: %d/%d passed", total - failures, total);
	if (failures) {
		printf(", %d FAILED", failures);
		printf("\n");
		return 1;
	}
	printf("\n");
	return 0;
}