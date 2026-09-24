/* 2A-5 joint — file-backed cross-process round-trip.
 * Self-exec harness (see libsepal/islet 2A-5 siblings): orchestrator
 * fork+exec's itself per phase so every reopen is a fresh process reading
 * what the prior phase's corm_save()+destructor flushed to disk; never
 * corm_close (no-close invariant). Every worker burns handle 0 first
 * ((void)joint_init(NULL) — the documented jd-0 ↔ NULL collision,
 * joint_axis_store_test.c:60-68), then rec_axis_open(path).
 *
 *   seed     → rec_axis_store(13,"2026-01-15:hello world") open interval
 *              and (14,"2026-03-01,2026-06-01") atomic, via file-backed ctx
 *   verify1  → reopen (fresh proc) → joint_iter: at 2026-02-01 has 13 not 14;
 *              at 2026-04-01 has 14; readback(13)=2026-01-15
 *              (proves file-backed id-index backfill on reopen)
 *   unstore  → reopen → rec_axis_unstore(13)
 *   verify2  → reopen → 13 gone, 14 intact, readback(13) NULL
 */

#include "../include/ttypt/joint.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ttypt/corm.h>

#define DB_PATH "/tmp/test_joint_roundtrip.corm"

static int
present(unsigned jd, time_t when, unsigned id)
{
	time_t a, b;
	unsigned count, who;
	joint_cur_t c = joint_iter(jd, when, when + 1);
	while (joint_next(&a, &b, &count, &who, &c))
		if (who == id)
			return 1;
	return 0;
}

static time_t
D(const char *s)
{
	char buf[64];
	strcpy(buf, s);
	return sscantime(buf);
}

static void
unlink_stale(void)
{
	unlink(DB_PATH);
}

static int
phase_seed(void)
{
	(void)joint_init(NULL); /* burn handle 0 */
	void *ctx = rec_axis_open(DB_PATH);
	unsigned jd;
	if (!ctx)
		return 1;
	jd = (unsigned)(uintptr_t)ctx;
	if (rec_axis_store(ctx, NULL, 13, "2026-01-15:hello world") != 0)
		return 1;
	if (rec_axis_store(ctx, NULL, 14, "2026-03-01,2026-06-01") != 0)
		return 1;
	if (!present(jd, D("2026-02-01"), 13))
		return 1;
	if (!present(jd, D("2026-04-01"), 14))
		return 1;
	corm_save();
	return 0;
}

static int
phase_verify1(void)
{
	(void)joint_init(NULL);
	void *ctx = rec_axis_open(DB_PATH);
	unsigned jd;
	char *blob = NULL;
	size_t n = 0;
	if (!ctx)
		return 1;
	jd = (unsigned)(uintptr_t)ctx;
	if (!present(jd, D("2026-02-01"), 13))
		return 1;
	if (present(jd, D("2026-02-01"), 14))
		return 1; /* atomic hasn't started */
	if (!present(jd, D("2026-04-01"), 14))
		return 1;
	if (!present(jd, D("2026-04-01"), 13))
		return 1; /* open interval still present */
	if (rec_axis_readback(ctx, 13, &blob, &n) != 0 || !blob)
		return 1;
	/* open interval readback is the start date alone */
	if (strcmp(blob, "2026-01-15") != 0)
		return 1;
	free(blob);
	blob = NULL;
	if (rec_axis_readback(ctx, 14, &blob, &n) != 0 || !blob)
		return 1;
	if (strcmp(blob, "2026-03-01,2026-06-01") != 0)
		return 1;
	free(blob);
	return 0;
}

static int
phase_unstore(void)
{
	(void)joint_init(NULL);
	void *ctx = rec_axis_open(DB_PATH);
	unsigned jd;
	if (!ctx)
		return 1;
	jd = (unsigned)(uintptr_t)ctx;
	(void)jd;
	if (rec_axis_unstore(ctx, 13) != 0)
		return 1;
	/* isolation: 14 still readable in this phase (pre-save) */
	{
		char *blob = NULL;
		size_t n = 0;
		if (rec_axis_readback(ctx, 14, &blob, &n) != 0 || !blob)
			return 1;
		free(blob);
	}
	corm_save();
	return 0;
}

static int
phase_verify2(void)
{
	(void)joint_init(NULL);
	void *ctx = rec_axis_open(DB_PATH);
	unsigned jd;
	char *blob = (char *)0x1;
	size_t n = 1;
	if (!ctx)
		return 1;
	jd = (unsigned)(uintptr_t)ctx;
	if (present(jd, D("2026-02-01"), 13))
		return 1;
	if (!present(jd, D("2026-04-01"), 14))
		return 1;
	if (rec_axis_readback(ctx, 13, &blob, &n) != 0)
		return 1;
	if (blob != NULL || n != 0)
		return 1;
	{
		char *b2 = NULL;
		size_t n2 = 0;
		if (rec_axis_readback(ctx, 14, &b2, &n2) != 0 || !b2)
			return 1;
		if (strcmp(b2, "2026-03-01,2026-06-01") != 0)
			return 1;
		free(b2);
	}
	/* idempotence after reopen still holds */
	if (rec_axis_unstore(ctx, 13) != 0)
		return 1;
	return 0;
}

static int
run_phase(const char *prog, const char *mode)
{
	pid_t pid;
	int st = 1;
	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0) {
		execl(prog, prog, mode, (char *)NULL);
		_exit(127);
	}
	if (waitpid(pid, &st, 0) < 0)
		return 1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

int
main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : NULL;
	if (mode) {
		if (strcmp(mode, "seed") == 0)
			return phase_seed();
		if (strcmp(mode, "verify1") == 0)
			return phase_verify1();
		if (strcmp(mode, "unstore") == 0)
			return phase_unstore();
		if (strcmp(mode, "verify2") == 0)
			return phase_verify2();
		fprintf(stderr, "unknown mode: %s\n", mode);
		return 2;
	}
	printf("=== 2A-5 joint file-backed cross-process round-trip ===\n");
	unlink_stale();
	if (run_phase(argv[0], "seed") != 0) {
		printf("seed phase failed\n");
		return 1;
	}
	if (run_phase(argv[0], "verify1") != 0) {
		printf("reopen → query finds ref: FAILED\n");
		return 1;
	}
	printf("reopen → query finds ref: ok (file-backed id backfill)\n");
	if (run_phase(argv[0], "unstore") != 0) {
		printf("unstore phase failed\n");
		return 1;
	}
	if (run_phase(argv[0], "verify2") != 0) {
		printf("reopen → absent: FAILED\n");
		return 1;
	}
	printf("reopen → absent after unstore: ok\n");
	unlink_stale();
	printf("ALL ROUND-TRIP PHASES PASSED\n");
	return 0;
}
