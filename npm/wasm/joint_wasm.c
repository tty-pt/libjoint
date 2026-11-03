#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ttypt/iso.h>

typedef struct {
    uint32_t id;
    int64_t start;
    int64_t stop;
} wasm_interval_t;

#define MAX_WASM_INTERVALS 4096
static wasm_interval_t g_intervals[MAX_WASM_INTERVALS];
static size_t g_count = 0;

ISO_EXPORT(void, joint_wasm_clear, void) {
    g_count = 0;
}

ISO_EXPORT(int, joint_wasm_record, uint32_t id, int64_t start, int64_t stop) {
    if (g_count >= MAX_WASM_INTERVALS) return -1;
    g_intervals[g_count].id = id;
    g_intervals[g_count].start = start;
    g_intervals[g_count].stop = stop;
    g_count++;
    return 0;
}

ISO_EXPORT(size_t, joint_wasm_intersect, int64_t start, int64_t stop, uint32_t *out_ids, size_t max_out) {
    size_t matched = 0;
    for (size_t i = 0; i < g_count && matched < max_out; i++) {
        if (start < g_intervals[i].stop && stop > g_intervals[i].start) {
            out_ids[matched++] = g_intervals[i].id;
        }
    }
    return matched;
}
