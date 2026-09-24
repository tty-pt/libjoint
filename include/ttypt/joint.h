#ifndef JOINT_H
#define JOINT_H

/**
 * @file joint.h
 * @brief Public API for the Interval Tree Library (libjoint).
 *
 * Provides an efficient interval tree implementation for tracking
 * time-based intervals with persistence support. Built on libcorm
 * for high-performance querying and sorted iteration.
 *
 * Key features:
 * - Store and query time intervals with associated IDs
 * - File-based persistence via corm
 * - Efficient intersection queries
 * - Multiple secondary indexes (by max time, by ID)
 * - Sorted iteration support
 *
 * ID-uniformity contract (see RECALL-KERNEL.md):
 * entity ids are stored and compared internally as `unsigned` (32-bit) --
 * this is intentional, not a gap. The uniformity boundary other axis
 * libraries (libsepal, libstoma) share is `rec_ref_t` (uint32_t, from
 * rec.h), and it is enforced at the *fill* boundary only:
 * rec_axis_fill_interval() widens every `who` to `rec_ref_t` before
 * pushing it into the caller's rec_set_t. Internal storage width is each
 * library's own choice (see libislet's identical convention for its
 * per-cell values). Do not assume ids above UINT32_MAX round-trip through
 * joint_start()/joint_stop()/joint_next().
 *
 * @see corm.h for underlying storage implementation
 */

#include <time.h>
#include <sys/types.h>
#include <ttypt/rec.h>

/**
 * @brief Maximum length for ISO-8601 date string buffers.
 *
 * Used by printtime() for formatting timestamps.
 */
#define DATE_MAX_LEN 20

/**
 * @brief Opaque iterator handle for interval tree traversal.
 *
 * Created by joint_iter() and used with joint_next() to traverse
 * intervals that intersect a time range. Must not be freed
 * directly by the user.
 *
 * @see joint_iter
 * @see joint_next
 */
typedef void * joint_cur_t;

/**
 * @brief Initialize an interval tree database.
 *
 * Creates or opens a file-backed interval tree with three
 * internal corm databases:
 * - "ti": Primary map (interval -> interval)
 * - "max": Secondary index sorted by interval max time
 * - "id": Secondary index sorted by entity ID
 *
 * @param[in] fname Path to database file, or NULL for in-memory only.
 *                  If provided, data persists across program runs.
 *
 * @return Database handle for use with other joint_* functions.
 *         Handle is an integer ID that remains valid until
 *         process exit (no explicit close needed).
 *
 * @note File persistence uses corm's automatic save-on-exit.
 *       Multiple databases can share one file via different names.
 *
 * @see joint_start
 * @see joint_stop
 * @see joint_iter
 *
 * Example:
 * @code
 * // Create persistent interval tree
 * uint32_t jd = joint_init("events.corm");
 * 
 * // Add intervals
 * joint_start(jd, timestamp1, user_id);
 * joint_stop(jd, timestamp2, user_id);
 * 
 * // Query overlapping intervals
 * joint_cur_t cur = joint_iter(jd, start_time, end_time);
 * time_t min, max;
 * uint32_t count, who;
 * while (joint_next(&min, &max, &count, &who, &cur)) {
 *     printf("Interval: %ld-%ld, ID: %u\n", min, max, who);
 * }
 * @endcode
 */
unsigned joint_init(char *fname);

/**
 * @brief Close an interval tree database and free resources.
 *
 * Closes all associated corm databases for the given handle,
 * ensuring data is persisted to disk. After calling this function,
 * the handle should not be used again.
 *
 * @param[in] jd Database handle from joint_init().
 *
 * @note This function should be called before re-opening the same
 *       database file to ensure data persistence.
 *
 * @see joint_init
 */
void joint_close(unsigned jd);

/**
 * @brief Start a new interval for an entity.
 *
 * Records that an entity (identified by id) began an activity
 * at the given timestamp. The interval remains open (max = infinity)
 * until closed with joint_stop().
 *
 * @param[in] jd Database handle from joint_init().
 * @param[in] ts  Timestamp when the interval begins.
 *               Must be in range [LONG_MIN/2, LONG_MAX/2] to avoid
 *               conflicts with internal sentinel values.
 * @param[in] id  Entity identifier (e.g., user ID, session ID).
 *               Cannot be UINT32_MAX (reserved as internal sentinel).
 *
 * @return 0 on success (new interval started).
 *         1 if entity already has an open interval at this time
 *         (duplicate start attempt).
 *         -1 on validation error (check errno):
 *           - ERANGE: timestamp outside valid range
 *           - EINVAL: entity ID is UINT32_MAX
 *
 * @note If an entity already has an open interval, this function
 *       returns 1 and does not create a duplicate.
 *
 * @see joint_stop
 * @see joint_init
 */
int joint_start(unsigned jd, time_t ts, unsigned id);

/**
 * @brief Stop an interval for an entity.
 *
 * Closes the most recent open interval for the given entity,
 * setting its end time to the provided timestamp. If no open
 * interval exists, creates a new interval ending at this time
 * with start = -infinity.
 *
 * @param[in] jd Database handle from joint_init().
 * @param[in] ts  Timestamp when the interval ends.
 *               Must be in range [LONG_MIN/2, LONG_MAX/2] to avoid
 *               conflicts with internal sentinel values.
 * @param[in] id  Entity identifier.
 *               Cannot be UINT32_MAX (reserved as internal sentinel).
 *
 * @return 0 on success (existing interval closed).
 *         1 if no open interval existed (created backward interval).
 *         -1 on validation error (check errno):
 *           - ERANGE: timestamp outside valid range
 *           - EINVAL: entity ID is UINT32_MAX
 *
 * @note This function handles the case where stop is called
 *       before start by creating an interval from -infinity.
 *
 * @see joint_start
 * @see joint_init
 */
int joint_stop(unsigned jd, time_t ts, unsigned id);

/**
 * @brief Begin iterating over intervals that intersect a time range.
 *
 * Creates an iterator for all intervals that overlap with the
 * specified time range [start, end). The iterator returns split
 * intervals that show which entities were present during each
 * sub-interval.
 *
 * @param[in] jd   Database handle from joint_init().
 * @param[in] start Start of the query time range (inclusive).
 * @param[in] end   End of the query time range (exclusive).
 *
 * @return Opaque iterator handle for use with joint_next().
 *         The iterator remains valid until all results are consumed
 *         or the process exits. No explicit cleanup needed.
 *
 * @note The iterator computes split intervals that show periods
 *       where the set of present entities changes. Use joint_next()
 *       to retrieve each split interval and its associated entities.
 *
 * @see joint_next
 * @see joint_init
 * @see joint_start
 * @see joint_stop
 */
joint_cur_t joint_iter(unsigned jd, time_t start, time_t end);

/**
 * @brief Get the next interval and entity from an iterator.
 *
 * Retrieves the next split interval from the iterator, along with
 * one entity ID that was present during that interval. Call repeatedly
 * to get all entities for each interval.
 *
 * @param[out] min   Start time of this interval segment.
 * @param[out] max   End time of this interval segment.
 * @param[out] count Total number of entities present in this segment.
 * @param[out] who   Entity ID for this iteration.
 * @param[in,out] c  Iterator handle from joint_iter().
 *
 * @return 1 if an entity was retrieved (continue iteration).
 *         0 if no more entities/intervals (iteration complete).
 *
 * @note To get all entities for each interval, keep calling joint_next()
 *       until count entities are retrieved for that time segment.
 *       The iterator automatically advances to the next interval
 *       when all entities are consumed.
 *
 * @see joint_iter
 */
int joint_next(time_t *min, time_t *max, unsigned *count, unsigned *who, joint_cur_t *c);

/**
 * @brief Parse an ISO-8601 date string or Unix timestamp.
 *
 * Converts a date/time string to a Unix timestamp (time_t).
 * Supports multiple formats:
 * - ISO-8601 date+time: "2024-12-25T14:30:00"
 * - ISO-8601 date only: "2024-12-25"
 * - Unix timestamp: "1735139400"
 *
 * @param[in] buf String containing date/time to parse.
 *
 * @return Parsed timestamp as time_t.
 *         On error, the behavior depends on the input format.
 *
 * @note For date-only format, time is assumed to be 00:00:00.
 *       The function uses the system's local timezone.
 *
 * @see printtime
 */
time_t sscantime(char *buf);

/**
 * @brief Format a Unix timestamp as an ISO-8601 date string.
 *
 * Converts a time_t value to a human-readable date string.
 * Special values -infinity and +infinity are formatted as
 * "-inf" and "inf" respectively.
 *
 * Output format depends on the time component:
 * - If time is 00:00:00: "YYYY-MM-DD" (date only)
 * - Otherwise: "YYYY-MM-DDTHH:MM:SS" (full ISO-8601)
 *
 * @param[out] buf Buffer to store formatted string (must be
 *                 at least DATE_MAX_LEN bytes).
 * @param[in]  ts  Timestamp to format.
 *
 * @note The output uses the system's local timezone.
 *       Special handling for infinity values (min/max time_t).
 *
 * @see sscantime
 * @see DATE_MAX_LEN
 */
void printtime(char buf[DATE_MAX_LEN], time_t ts);

/**
 * @brief Recall-kernel time-axis filler (see ttypt/rec.h).
 *
 * Every entity present at any point in [a, b) becomes one ref in `out`
 * (rec_ref_t == the joint entity id, widened from the internal 32-bit
 * `unsigned` -- see the ID-uniformity contract note at the top of this
 * file). Refs are appended (additive) and
 * `out` is sealed (duplicates across split segments are deduped by the
 * seal). Compatible with rec_axis_t.fill via the "joint" axis registered
 * by this library's constructor (ctx = the joint_init() handle, cast
 * through a void*).
 *
 * @param[in] jd  Database handle from joint_init().
 * @param[in] a   Start of the range (inclusive).
 * @param[in] b   End of the range (exclusive).
 * @param[out] out Recall-kernel set to fill.
 * @return 0 on success, -1 if out is NULL.
 */
int rec_axis_fill_interval(unsigned jd, time_t a, time_t b, rec_set_t *out);

/**
 * @brief Erase every interval an entity owns (Phase 2A inverse of start/stop).
 *
 * Walks the entity's id-index entries (corm_get_multi on the `id` secondary),
 * recovers each `struct ti`, and deletes it from the primary `ti` map — the
 * associated `max`/`id` indexes are maintained automatically by corm_assoc.
 * Cost O(intervals-of-id), never O(store); zero new stored state.
 *
 * @param[in] jd Database handle from joint_init().
 * @param[in] id Entity identifier (cannot be UINT32_MAX, reserved sentinel).
 *
 * @return 0 on success (even when the entity owns nothing — idempotent).
 *         -1 on validation error (errno = EINVAL: entity ID is UINT32_MAX).
 *
 * @note Neighboring entities' intervals are unaffected. After erase the
 *       entity can joint_start() a fresh timeline.
 */
int joint_erase(unsigned jd, unsigned id);

/*
 * Phase 2A store/unstore/readback adapters (RECALL-KERNEL.md, optional
 * CLI-specific — not libcorm core API). ctx is the jd handle widened to a
 * pointer via uintptr_t (same cast rec_axis_open/joint_fill use); spec is
 * reserved (NULL). The consumer passes (ref, value) blindly; joint parses
 * the WHOLE value string in its own ordered grammar:
 *   "A" or "<DATE>:<anything>"  -> open interval starting at A
 *   ",B"                        -> close an open interval / backfill [-inf,B)
 *   "A,B" (B>A)                 -> atomic interval [A,B)
 *   else                        -> EINVAL
 * Native 1s (already-present / back-filled) absorb to 0. Exact-duplicate
 * restates are no-ops via an id-index exact-match guard. store: 0 ok, -1
 * errno EINVAL (bad grammar/args) / ERANGE (out of domain). unstore: removes
 * every interval the ref owns, idempotent absent -> 0. readback: one
 * malloc'd NUL-joined buffer of entries in the store grammar (closed "A,B",
 * open "A", backfill ",B"), n_out = display chars; absent -> NULL/0, still 0.
 */
int rec_axis_store(void *ctx, const char *spec, rec_ref_t ref, const char *value);
int rec_axis_unstore(void *ctx, rec_ref_t ref);
int rec_axis_readback(void *ctx, rec_ref_t ref, char **blob_out, size_t *n_out);

/*
 * rec_axis_open (RECALL-KERNEL.md "rec_axis_open convention", optional CLI-open convention,
 * not part of libcorm's core rec_query registry API): opens a joint
 * store from an opaque spec string and returns the ctx a caller then
 * passes to rec_axis_set_ctx(). spec is the joint_init() filename, or
 * empty/NULL for an in-memory store; the returned ctx is the jd handle
 * widened to a pointer via uintptr_t (same cast joint_fill uses).
 * Handle 0 is burned once per process: the jd-0 ↔ NULL collision means
 * the recall kernel reads a NULL ctx as "not bound", so rec_axis_open
 * never hands out 0 (the joint_axis_store_test.c workers' manual burn
 * stays valid — burning twice is harmless).
 */
void *rec_axis_open(const char *spec);

#endif
