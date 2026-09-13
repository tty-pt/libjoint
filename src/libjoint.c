#define _DEFAULT_SOURCE
#ifndef __OpenBSD__
#define _XOPEN_SOURCE
#endif
#include "../include/ttypt/joint.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ttypt/queue.h>
#include <ttypt/qmap.h>
#include <ttypt/idm.h>
#include <ttypt/qsys.h>
#include <ttypt/rec.h>

#ifdef _WIN32
static char *strptime(const char *s, const char *fmt, struct tm *tm) {
	int y, mo, d, h, mi, sec;
	if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) == 6) {
		tm->tm_year = y - 1900; tm->tm_mon = mo - 1; tm->tm_mday = d;
		tm->tm_hour = h; tm->tm_min = mi; tm->tm_sec = sec;
		return (char *)s + 19;
	}
	if (sscanf(s, "%d-%d-%d", &y, &mo, &d) == 3) {
		tm->tm_year = y - 1900; tm->tm_mon = mo - 1; tm->tm_mday = d;
		tm->tm_hour = 0; tm->tm_min = 0; tm->tm_sec = 0;
		return (char *)s + 10;
	}
	(void)fmt;
	return NULL;
}
#endif

#ifdef __OpenBSD__
#define TS_MIN LLONG_MIN
#define TS_MAX LLONG_MAX
#else
#define TS_MIN LONG_MIN
#define TS_MAX LONG_MAX
#endif

#define TI_DBS_MAX 512
#define TI_MASK 0xFFFF

enum cflags {
	JOINT_AHEAD = 1, // first element
	JOINT_HD = 2, // iterating inside split
};

struct ti {
	time_t min, max;
	uint32_t who;
};

struct isplit {
	time_t ts;
	int max;
	uint32_t who;
};

struct match {
	struct ti ti;
};

struct match_arena {
	struct match *buf;   /* contiguous, cache-resident */
	size_t len;          /* used */
	size_t cap;          /* allocated */
};

/* append one match slot; grows by doubling */
static inline struct match *
match_arena_push(struct match_arena *restrict m)
{
	if (m->len == m->cap) {
		size_t ncap = m->cap ? m->cap * 2 : 16;
		struct match *nbuf = (struct match *) realloc(m->buf, ncap * sizeof(struct match));
		CBUG(!nbuf, "out of memory in match_arena_push");
		m->buf = nbuf;
		m->cap = ncap;
	}
	return &m->buf[m->len++];
}

static inline void
match_arena_drop(struct match_arena *m)
{
	free(m->buf);
	m->buf = NULL;
	m->len = m->cap = 0;
}

struct split {
	time_t min;
	time_t max;
	uint32_t *ids;   /* contiguous LIFO stack, points into the query arena */
	unsigned count;  /* number of ids */
	unsigned pop;    /* LIFO cursor: count..0 */
	TAILQ_ENTRY(split) entry;
};

TAILQ_HEAD(split_tailq, split);

/* Query-level arena: split structs + id stacks drawn from linked blocks.
 * Blocks are allocated on demand and NEVER moved or freed for the lifetime of
 * the query, so TAILQ pointers and split->ids remain stable even while
 * splits_fill appends more splits (this is the invariant the previous
 * growable-buffer design broke). */
struct bblock {
	struct bblock *next;
	size_t cap, used;   /* body follows */
};

struct split_arena {
	struct bblock *blk;   /* current block */
};

static inline void *
arena_alloc(struct split_arena *a, size_t nbytes)
{
	const size_t align = 16;
	size_t need = (nbytes + align - 1) & ~((size_t) align - 1);
	struct bblock *b = a->blk;

	if (!b || b->cap - b->used < need) {
		size_t cap = b ? b->cap * 2 : 8192;
		if (cap < need)
			cap = need;
		struct bblock *nb = (struct bblock *) malloc(sizeof(*nb) + cap);
		CBUG(!nb, "out of memory in arena");
		nb->next = b;
		nb->cap = cap;
		nb->used = 0;
		a->blk = nb;
		b = nb;
	}

	void *p = (char *)(b + 1) + b->used;
	b->used += need;
	return p;
}

/* Ephemeral open-addressing set of entity ids (tombstone deletions),
 * replacing the per-splits_get temp qmap. key[i] stores id+1 (0 = empty);
 * del[i] marks a tombstone. Grows by doubling when live+dead occupancy
 * reaches half capacity (compacting tombstones). Lives for one endpoint
 * sweep; split_create reads the present set by scanning slots. */
struct who_set {
	uint32_t *key;
	uint8_t *del;
	size_t cap;    /* power of 2 */
	size_t n;      /* live count */
	size_t used;   /* live + tombstone */
};

static inline void
who_set_init(struct who_set *s)
{
	s->key = NULL;
	s->del = NULL;
	s->cap = s->n = s->used = 0;
}

static inline void
who_set_free(struct who_set *s)
{
	free(s->key);
	free(s->del);
	who_set_init(s);
}

static inline size_t
who_set_slot(const struct who_set *s, uint32_t stored)
{
	return (size_t)((uint32_t)(stored * 2654435761u)) & (s->cap - 1);
}

static void
who_set_grow(struct who_set *s)
{
	size_t ncap = s->cap ? s->cap * 2 : 64;
	uint32_t *nk = (uint32_t *) calloc(ncap, sizeof(uint32_t));
	uint8_t *nd = (uint8_t *) calloc(ncap, sizeof(uint8_t));

	if (!nk || !nd) {
		free(nk);
		free(nd);
		CBUG(1, "out of memory in who_set_grow");
	}

	if (s->cap) {
		for (size_t i = 0; i < s->cap; i++) {
			if (s->key[i] && !s->del[i]) {
				uint32_t k = s->key[i];
				size_t j = (size_t)((uint32_t)(k * 2654435761u)) & (ncap - 1);
				while (nk[j])
					j = (j + 1) & (ncap - 1);
				nk[j] = k;
			}
		}
	}

	free(s->key);
	free(s->del);
	s->key = nk;
	s->del = nd;
	s->cap = ncap;
	s->used = s->n;
}

static inline void
who_set_put(struct who_set *s, uint32_t id)
{
	uint32_t stored = id + 1;
	size_t j, hole = (size_t) -1;

	if (s->used >= s->cap / 2)
		who_set_grow(s);

	j = who_set_slot(s, stored);
	while (s->key[j]) {
		if (s->key[j] == stored && !s->del[j])
			return;   /* already live */
		if (s->del[j] && hole == (size_t) -1)
			hole = j;
		j = (j + 1) & (s->cap - 1);
	}
	if (hole != (size_t) -1)
		j = hole;
	s->key[j] = stored;
	s->del[j] = 0;
	s->n++;
	s->used++;
}

static inline void
who_set_del(struct who_set *s, uint32_t id)
{
	uint32_t stored = id + 1;
	size_t j = who_set_slot(s, stored);

	while (s->key[j]) {
		if (s->key[j] == stored && !s->del[j])
			break;
		j = (j + 1) & (s->cap - 1);
	}
	if (!s->key[j])
		return;   /* not present */
	s->del[j] = 1;
	s->n--;
}

struct tidbs {
	uint32_t ti; // keys and values are struct ti
	uint32_t max; // secondary DB (BTREE) with interval max as key
	uint32_t id; // secondary DB (BTREE) with ids as primary key
} ti_dbs[TI_DBS_MAX];

const time_t mtinf = (time_t) TS_MIN; // minus infinite
const time_t tinf = (time_t) TS_MAX; // infinite

static int ti_first = 1;

static idm_t idm;

uint32_t qm_ti, qm_time, qm_id;

/* get timestamp from ISO-8601 date string */
time_t sscantime(char *buf) {
	char *aux;
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	aux = strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm);
	if (!aux && !strptime(buf, "%Y-%m-%d", &tm)) {
		char *endptr;
		errno = 0; /* Reset errno before strtoull */
		unsigned long long int timestamp = strtoull(buf, &endptr, 10);
		CBUG(errno || *endptr != '\0' || buf == endptr, "Invalid date or timestamp");
		return (time_t)timestamp;
	}

	tm.tm_isdst = -1;
	return mktime(&tm);
}

/* get ISO-8601 date string from timestamp
 *
 * only use this for debug (memory leak), or free pointer
 */
void printtime(char buf[DATE_MAX_LEN], time_t ts) {
	struct tm tm;

	if (ts == mtinf) {
		strcpy(buf, "-inf");
		return;
	}

	if (ts == tinf) {
		strcpy(buf, "inf");
		return;
	}

	tm = *localtime(&ts);

	if (tm.tm_sec || tm.tm_min || tm.tm_hour)
		strftime(buf, DATE_MAX_LEN, "%Y-%m-%dT%H:%M:%S", &tm);
	else
		strftime(buf, DATE_MAX_LEN, "%Y-%m-%d", &tm);
}

/******
 * key ordering compare functions
 ******/

/* compare two time intervals (for sorting BST items) */
static int
timax_cmp(const void * const a_r,
		const void * const b_r,
		size_t len UNUSED)
{
	time_t	a = * (time_t *) a_r,
		b = * (time_t *) b_r;
	return b > a ? -1 : (a > b ? 1 : 0);
}

/* compare two person ids (for sorting BST items) */
static int
tiid_cmp(const void * const a_r,
		const void * const b_r,
		size_t len UNUSED)
{
	uint32_t a = * (uint32_t *) a_r,
		 b = * (uint32_t *) b_r;
	return b > a ? -1 : (a > b ? 1 : 0);
}

/******
 * qmap_assoc key callbacks
 *
 * The `max` and `id` secondary maps are wired to the primary `ti` map via
 * qmap_assoc (QM_SORTED | QM_MULTIVALUE), so every qmap_put/qmap_del on the
 * primary automatically maintains both indexes — including duplicate
 * secondary keys (two intervals with the same max, or multiple intervals for
 * the same entity). qmap >= b1bc322 supports duplicate keys properly
 * (QM_MULTIVALUE; QM_RANGE iterates all duplicates — bug3).
 ******/

/* secondary key for the max index: the interval's max timestamp */
static void
assoc_max_time_cb(const void **skey,
		const void * const pkey UNUSED,
		const void * const value,
		void *userdata UNUSED)
{
	*skey = &((struct ti *)value)->max;
}

/* secondary key for the id index: the interval's entity id */
static void
assoc_id_cb(const void **skey,
		const void * const pkey UNUSED,
		const void * const value,
		void *userdata UNUSED)
{
	*skey = &((struct ti *)value)->who;
}

/******
 * Database initializers
 ******/

__attribute__((constructor))
static void
libjoint_init(void)
{
	qm_ti = qmap_reg(sizeof(struct ti));
	qm_time = qmap_reg(sizeof(time_t));
	qm_id = qmap_reg(sizeof(uint32_t));
	qmap_cmp_set(qm_id, tiid_cmp);
}

/* initialize ti dbs */
static void
tidbs_init(struct tidbs *dbs, char *fname)
{
	uint32_t flags = 0;  /* QM_MIRROR optional in qmap v0.7.0+, not needed for persistence */

	/* Only persist the primary 'ti' database; secondary indexes are in-memory only */
	dbs->ti = qmap_open(fname, "ti", qm_ti, qm_ti, TI_MASK, flags);
	dbs->max = qmap_open(NULL, NULL, qm_time, qm_ti, TI_MASK, QM_SORTED | QM_MULTIVALUE);
	dbs->id = qmap_open(NULL, NULL, qm_id, qm_ti, TI_MASK, QM_SORTED | QM_MULTIVALUE);

	qmap_cmp_set(qm_time, timax_cmp);

	/* Wire the secondary indexes to the primary 'ti' map via qmap_assoc:
	 * every qmap_put/qmap_del on the primary automatically maintains both
	 * indexes (assoc_max_time_cb/assoc_id_cb pick the secondary key), and
	 * qmap_assoc itself backfills the indexes from any entries already
	 * present in the primary (e.g. loaded from the file when it is
	 * reopened). Since qmap b1bc322 duplicate secondary keys are properly
	 * supported (QM_MULTIVALUE; QM_RANGE iterates all duplicates — bug3),
	 * so two intervals with the same max time, or multiple intervals for
	 * the same entity, are all indexed. */
	qmap_assoc(dbs->max, dbs->ti, assoc_max_time_cb, NULL);
	qmap_assoc(dbs->id, dbs->ti, assoc_id_cb, NULL);
}

/******
 * ti (struct ti to struct ti primary db) related functions
 ******/
 
/* insert a time interval */
static void
ti_insert(struct tidbs *dbs, uint32_t id, time_t start, time_t end)
{
	struct ti ti = { .min = start, .max = end, .who = id };
	qmap_put(dbs->ti, &ti, &ti);
}

/* finish the last found interval at the provided timestamp for a certain
 * person id
 */
static void
ti_finish_last(struct tidbs *dbs, uint32_t id, time_t end)
{
	struct ti ti, old_ti;
	const void *key, *value;
	/* Only scan this entity's intervals (via the id index) instead of all
	 * intervals. The chain-based get_multi yields the same duplicate set
	 * as the old QM_RANGE scan, without triggering a sorted-index rebuild. */
	uint32_t c = qmap_get_multi(dbs->id, &id);
	int found = 0;

	if (c == QM_MISS)
		return;  /* No intervals for this entity at all */

	while (qmap_next(&key, &value, c)) {
		memcpy(&ti, value, sizeof(ti));

		/* Find the open interval (max=tinf) for this entity */
		if (ti.who == id && ti.max == tinf) {
			memcpy(&old_ti, &ti, sizeof(ti));
			found = 1;
			qmap_fin(c);
			break;
		}
	}

	if (!found) {
		return;  /* No open interval found */
	}

	/* Delete old interval and insert updated one */
	qmap_del(dbs->ti, &old_ti);
	
	ti.max = end;
	qmap_put(dbs->ti, &ti, &ti);
}

/* intersect an interval with an AVL of intervals */
static inline unsigned
ti_intersect(struct tidbs *dbs, struct match_arena *matches, time_t min, time_t max)
{
	struct ti tmp;
	const void *key, *value;
	int ret = 0;

	match_arena_drop(matches);
	/* Start at the first interval with max >= min (via the max index) —
	 * a lower-bound range (all duplicate maxes included) — instead of
	 * scanning every interval; the match predicate below is kept
	 * identical. QM_RANGE_GE is needed because on QM_MULTIVALUE maps
	 * plain QM_RANGE would only iterate duplicates of the exact key. */
	uint32_t c = qmap_iter(dbs->max, &min, QM_RANGE | QM_RANGE_GE);

	while (qmap_next(&key, &value, c)) {
		memcpy(&tmp, value, sizeof(struct ti));

		if (tmp.max >= min && tmp.min < max) {
			// its a match
			struct match *match = match_arena_push(matches);
			memcpy(&match->ti, &tmp, sizeof(tmp));
			ret++;
		}
	}
	
	qmap_fin(c);
	return ret;
}

int
ti_present(struct tidbs *dbs, time_t when, uint32_t who) {
	int ret = 0;
	struct ti tmp;
	/* Only scan this entity's intervals (via the id index) instead of all
	 * intervals; the predicate below is kept identical. The chain-based
	 * get_multi yields the same duplicate set as the old QM_RANGE scan,
	 * without triggering a sorted-index rebuild. */
	uint32_t c = qmap_get_multi(dbs->id, &who);
	const void *key, *value;

	if (c == QM_MISS)
		return ret;  /* No intervals for this entity at all */

	while (qmap_next(&key, &value, c)) {
		memcpy(&tmp, value, sizeof(struct ti));
		
		if (tmp.who == who && tmp.max > when && tmp.min <= when) {
			ret++;
			break;
		}
	}
	
	qmap_fin(c);
	return ret;
}

/******
 * matches related functions
 ******/

/* makes all provided matches lie within the provided interval [min, max] */
static inline void
matches_fix(struct match_arena *matches, time_t min, time_t max)
{
	for (size_t i = 0; i < matches->len; i++) {
		struct match *match = &matches->buf[i];
		if (match->ti.min < min)
			match->ti.min = min;
		if (match->ti.max > max)
			match->ti.max = max;
	}
}

/******
 * isplit related functions
 ******/

/* compares isplits, so that we can sort them */
static int
isplit_cmp(const void *ap, const void *bp)
{
	const struct isplit *a = (const struct isplit *)ap;
	const struct isplit *b = (const struct isplit *)bp;
	if (b->ts > a->ts)
		return -1;
	if (a->ts > b->ts)
		return 1;
	if (b->max > a->max)
		return -1;
	if (a->max > b->max)
		return 1;
	return 0;
}

// assumes isplits is of size matches_l * 2
/* creates intermediary isplits */
static inline struct isplit *
isplits_create(struct match_arena *matches) {
	struct isplit *isplits = (struct isplit *) malloc(sizeof(struct isplit) * matches->len * 2);

	for (size_t i = 0; i < matches->len; i++) {
		struct match *match = &matches->buf[i];
		struct isplit *isplit = isplits + i * 2;
		isplit->ts = match->ti.min;
		isplit->max = 0;
		isplit->who = match->ti.who;
		isplit++;
		isplit->ts = match->ti.max;
		isplit->max = 1;
		isplit->who = match->ti.who;
	}

	return isplits;
}

/******
 * split related functions
 ******/

/* Creates one split from its interval, and the list of people that are present
 * (allocated from the query-level arena; single pass over the present set)
 */
static inline struct split *
split_create(struct who_set *whos, time_t min, time_t max, struct split_arena *arena)
{
	struct split *split = (struct split *) arena_alloc(arena, sizeof(struct split));
	uint32_t *dst = NULL;
	uint32_t i = 0;

	if (whos->n) {
		dst = (uint32_t *) arena_alloc(arena, (size_t) whos->n * sizeof(uint32_t));
		for (size_t j = 0; j < whos->cap; j++) {
			if (whos->key[j] && !whos->del[j])
				dst[i++] = whos->key[j] - 1;
		}
	}

	split->min = min;
	split->max = max;
	split->ids = dst;
	split->count = whos->n;
	split->pop = whos->n;
	return split;
}

/* Creates splits from the intermediary isplit array */
static inline void
splits_create(
		struct who_set *whos,
		struct split_tailq *splits,
		struct isplit *isplits,
		size_t matches_l,
		struct split_arena *arena)
{
	size_t i;

	TAILQ_INIT(splits);

	for (i = 0; i < matches_l * 2 - 1; i++) {
		struct isplit *isplit = isplits + i;
		struct isplit *isplit2 = isplits + i + 1;
		struct split *split;
		time_t n, m;

		if (isplit->max)
			who_set_del(whos, isplit->who);
		else
			who_set_put(whos, isplit->who);

		n = isplit->ts;
		m = isplit2->ts;

		if (n == m)
			continue;

		split = split_create(whos, n, m, arena);
		TAILQ_INSERT_TAIL(splits, split, entry);
	}
}

/* From a list of matched intervals, this creates the tail queue of splits
 */
static void
splits_init(struct who_set *whos, struct split_tailq *splits, struct match_arena *matches, struct split_arena *arena)
{
	struct isplit *isplits;

	isplits = isplits_create(matches);
	qsort(isplits, matches->len * 2, sizeof(struct isplit), isplit_cmp);
	splits_create(whos, splits, isplits, matches->len, arena);
	free(isplits);
}

/* Obtains a tail queue of splits from the intervals that intersect the query
 * interval [min, max]
 */
static void
splits_get(struct split_tailq *splits, struct tidbs *dbs, time_t min, time_t max, struct split_arena *arena)
{
	struct who_set whos;
	struct match_arena matches = { 0 };
	uint32_t matches_l;

	who_set_init(&whos);
	matches_l = ti_intersect(dbs, &matches, min, max);
	
	/* If no matches, initialize empty split queue and return */
	if (matches_l == 0) {
		TAILQ_INIT(splits);
		who_set_free(&whos);
		return;
	}
	
	matches_fix(&matches, min, max);
	splits_init(&whos, splits, &matches, arena);
	match_arena_drop(&matches);
	who_set_free(&whos);
}

/* Inserts a tail queue of splits within another, before the element provided
 */
static inline void
splits_concat_before(
		struct split_tailq *target UNUSED,
		struct split_tailq *origin,
		struct split *before UNUSED)
{
	struct split *split, *tmp;
	TAILQ_FOREACH_SAFE(split, origin, entry, tmp) {
		TAILQ_REMOVE(origin, split, entry);
		TAILQ_INSERT_BEFORE(before, split, entry);
	}
}

/* Fills the spaces between splits (or on empty splits)
 * with splits from BST B, in order to resolve the situation
 * where none of the people are present for periods of time
 * within the billing period (the aforementined gaps).
 */
static inline void
splits_fill(struct tidbs *tidbs, struct split_tailq *splits, time_t min, time_t max, struct split_arena *arena)
{
	struct split *split, *tmp;
	time_t last_max;

	split = TAILQ_FIRST(splits);
	if (!split) {
		splits_get(splits, tidbs, min, max, arena);
		return;
	}

	last_max = min;

	if (split->min > last_max) {
		struct split_tailq more_splits;
		splits_get(&more_splits, tidbs, last_max, split->min, arena);
		splits_concat_before(splits, &more_splits, split);
	}

	last_max = split->max;

	TAILQ_FOREACH_SAFE(split, splits, entry, tmp) {
		if (!split->count) {
			struct split_tailq more_splits;
			splits_get(&more_splits, tidbs, split->min, split->max, arena);
			splits_concat_before(splits, &more_splits, split);
			TAILQ_REMOVE(splits, split, entry);
		}

		last_max = split->max;
	}

	if (max > last_max) {
		struct split_tailq more_splits;
		splits_get(&more_splits, tidbs, last_max, max, arena);
		TAILQ_CONCAT(splits, &more_splits, entry);
	}
}

/******
 * functions that process a valid type of line
 ******/

static inline int UNUSED
joint_exists(uint32_t jd, time_t ts, uint32_t id UNUSED)
{
	struct tidbs *tidbs = &ti_dbs[jd];
	struct ti tmp;
	int ret = 0;
	uint32_t c = qmap_iter(tidbs->ti, NULL, 0);  // Iterate through ALL intervals
	const void *key, *value;

	while (qmap_next(&key, &value, c)) {
		memcpy(&tmp, value, sizeof(struct ti));
		if (tmp.max > ts && tmp.min <= ts) {
			ret = 1;
			break;
		}
	}
	
	qmap_fin(c);
	return ret;
}

int
joint_stop(uint32_t jd, time_t ts, uint32_t id)
{
	struct tidbs *tidbs = &ti_dbs[jd];

	/* Validate timestamp range to avoid conflicts with sentinels */
	if (ts < TS_MIN / 2 || ts > TS_MAX / 2) {
		errno = ERANGE;
		return -1;
	}

	/* Validate entity ID - UINT32_MAX is reserved as IDM_MISS sentinel */
	if (id == UINT32_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (!ti_present(tidbs, ts, id)) {
		ti_insert(tidbs, id, mtinf, ts);
		return 1;
	}

	ti_finish_last(tidbs, id, ts);
	return 0;
}

int
joint_start(uint32_t jd, time_t ts, uint32_t id)
{
	struct tidbs *tidbs = &ti_dbs[jd];

	/* Validate timestamp range to avoid conflicts with sentinels */
	if (ts < TS_MIN / 2 || ts > TS_MAX / 2) {
		errno = ERANGE;
		return -1;
	}

	/* Validate entity ID - UINT32_MAX is reserved as IDM_MISS sentinel */
	if (id == UINT32_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (ti_present(tidbs, ts, id))
		return 1;

	ti_insert(tidbs, id, ts, tinf);
	return 0;
}

struct joint_internal {
	uint32_t jd;
	struct split_tailq splits;
	struct split *next;
	struct split_arena arena;
};

joint_cur_t joint_iter(uint32_t jd, time_t start, time_t end)
{
	struct joint_internal *internal = malloc(sizeof(struct joint_internal));
	struct tidbs *tidbs = &ti_dbs[jd];
	memset(&internal->arena, 0, sizeof(internal->arena));
	splits_get(&internal->splits, tidbs, start, end, &internal->arena);
	splits_fill(tidbs, &internal->splits, start, end, &internal->arena);
	internal->next = TAILQ_FIRST(&internal->splits);
	internal->jd = jd;
	return internal;
}

int joint_next(time_t *min, time_t *max, uint32_t *count, uint32_t *who, joint_cur_t *c) {
	struct joint_internal *internal = *c;

	while (internal->next) {
		struct split *s = internal->next;
		if (s->pop > 0) {
			*who = s->ids[s->pop - 1];
			s->pop--;
			*min = s->min;
			*max = s->max;
			*count = s->count;
			return 1;
		}
		internal->next = TAILQ_NEXT(internal->next, entry);
	}
	return 0;
}

uint32_t joint_init(char *fname) {
	struct tidbs *tidbs;
	uint32_t id;

	if (ti_first) {
		idm = idm_init();
		ti_first = 0;
	}

	id = idm_new(&idm);
	tidbs = &ti_dbs[id];

	tidbs_init(tidbs, fname);

	return id;
}

void joint_close(unsigned jd) {
	struct tidbs *tidbs = &ti_dbs[jd];
	
	/* Persist data to disk BEFORE closing (only saves file-backed maps) */
	qmap_save();
	
	/* Close all databases */
	qmap_close(tidbs->ti);
	qmap_close(tidbs->max);
	qmap_close(tidbs->id);
	
	idm_del(&idm, jd);
}

/* ---- rec_query axis registration (time) ---- */

/*
 * Recall-kernel adapter (see rec.h): every entity present at any point in
 * [a, b) becomes one ref in `out`. `who` (unsigned) is used directly as
 * rec_ref_t; duplicates across split segments are fine, rec_set_seal
 * dedups. 0 ok / -1 on NULL out.
 */
int rec_axis_fill_interval(unsigned jd, time_t a, time_t b, rec_set_t *out)
{
	joint_cur_t c;
	time_t min, max;
	unsigned count, who;

	if (!out)
		return -1;
	c = joint_iter(jd, a, b);
	while (joint_next(&min, &max, &count, &who, &c))
		rec_set_push(out, (rec_ref_t)who);
	rec_set_seal(out);
	return 0;
}

struct rec_joint_params {
	time_t a;
	time_t b;
};

static int joint_fill(void *ctx, void *params, rec_set_t *out)
{
	unsigned jd = (unsigned)(uintptr_t)ctx;
	const struct rec_joint_params *p = params;

	if (!p)
		return -1;
	return rec_axis_fill_interval(jd, p->a, p->b, out);
}

/*
 * Decode "a=2024-01-01 b=2024-06-01T12:00:00" into a heap-owned
 * rec_joint_params (freed never — one-shot CLI process lifetime, matches
 * the other axis decode fns). Each value is parsed with sscantime, so any
 * of its accepted formats (date, date+time, unix timestamp) works. Missing
 * a/b default to 0. Key=value style (rather than a single "a:b" string) is
 * used to avoid ambiguity with the colons inside ISO-8601 time-of-day
 * values that sscantime itself accepts.
 */
static void *joint_decode(const char *s)
{
	struct rec_joint_params *p;
	char *buf, *cur;

	if (!s)
		return NULL;
	p = calloc(1, sizeof(*p));
	buf = malloc(strlen(s) + 1);
	if (!p || !buf) {
		free(p);
		free(buf);
		return NULL;
	}
	strcpy(buf, s);
	cur = buf;
	while (*cur) {
		char *key, *val;

		while (*cur == ' ')
			cur++;
		if (!*cur)
			break;
		key = cur;
		while (*cur && *cur != '=' && *cur != ' ')
			cur++;
		if (*cur != '=') {
			if (*cur)
				cur++;
			continue;
		}
		*cur++ = '\0';
		val = cur;
		while (*cur && *cur != ' ')
			cur++;
		if (*cur)
			*cur++ = '\0';
		if (!strcmp(key, "a"))
			p->a = sscantime(val);
		else if (!strcmp(key, "b"))
			p->b = sscantime(val);
	}
	return p;
}

__attribute__((constructor)) static void joint_rec_axis_init(void)
{
	static const rec_axis_t joint_axis = {
		"joint", joint_fill, NULL, NULL, joint_decode
	};

	rec_axis_register(&joint_axis);
}

/*
 * rec_axis_open convention (RECALL-KERNEL.md): spec is the
 * `joint_init` filename, or empty/NULL for an in-memory store. Returns
 * the jd (a small unsigned handle, like `rec_axis_fill_interval`'s ctx)
 * widened to a pointer via uintptr_t, same cast the constructor's own
 * `joint_fill`/tests already use for `rec_axis_set_ctx`.
 */
void *rec_axis_open(const char *spec)
{
	unsigned jd;

	jd = joint_init(spec && *spec ? (char *)spec : NULL);
	return (void *)(uintptr_t)jd;
}
