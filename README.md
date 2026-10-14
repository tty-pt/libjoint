<p align="center">
  <img src="assets/logo.png" alt="libjoint logo" width="220" />
</p>

# libjoint

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![interval-tree](https://img.shields.io/badge/interval--tree-16A34A)](#)
[![Recall Kernel Time Axis](https://img.shields.io/badge/axis-time-blueviolet)](#)

> Interval-tree temporal presence engine for querying who was where, when, and with whom across historical time windows in pure C.

---

## Recall Kernel (Time Axis)

`libjoint` functions as the canonical **Time Axis** for temporal queries and recall composition:
- **Storage Engine**: Built on sorted [libcorm](https://github.com/tty-pt/libcorm) maps for high-throughput temporal intervals.
- **Cross-Axis Recall Kernel**: Composes temporal windows (`rec_axis_fill_interval`) with sibling axes: [libislet](https://github.com/tty-pt/libgeo) (Space), [libstoma](https://github.com/tty-pt/libstoma) (Text), and [libsepal](https://github.com/tty-pt/libsepal) (Meaning).

---

## Quickstart

Record entity presence windows and query concurrent overlaps in under 20 lines:

```c
#include <stdio.h>
#include <time.h>
#include <ttypt/joint.h>

int main(void) {
    // Open in-memory temporal database
    unsigned jd = joint_init(NULL);

    // Entity 1 active from t=1000 to t=2000; Entity 2 active from t=1500 to t=2500
    joint_start(jd, 1000, 1);
    joint_stop(jd, 2000, 1);

    joint_start(jd, 1500, 2);
    joint_stop(jd, 2500, 2);

    // Query co-presence between t=1200 and t=1800
    joint_cur_t cur = joint_iter(jd, 1200, 1800);
    time_t min_t, max_t;
    unsigned count, who;

    while (joint_next(&min_t, &max_t, &count, &who, &cur)) {
        printf("Entity %u active in [%ld, %ld) (concurrent: %u)\n",
               who, (long)min_t, (long)max_t, count);
    }
    return 0;
}
```

Compile and link:
```sh
cc presence_query.c $(pkg-config --cflags --libs joint)
```

---

## Core API (The Essential 20%)

| Function | Signature | Description |
| :--- | :--- | :--- |
| `joint_init` | `unsigned joint_init(const char *file)` | Open in-memory (`NULL`) or disk-persisted temporal database |
| `joint_start` | `int joint_start(jd, time_t ts, unsigned id)` | Mark entity presence started at timestamp |
| `joint_stop` | `int joint_stop(jd, time_t ts, unsigned id)` | Mark entity presence ended at timestamp |
| `joint_iter` | `joint_cur_t joint_iter(jd, time_t a, time_t b)` | Create query cursor decomposed into constant-presence slices |
| `joint_next` | `int joint_next(&min, &max, &count, &who, &cur)` | Advance iterator returning active entities per time slice |
| `rec_axis_fill_interval` | `int rec_axis_fill_interval(jd, a, b, set)` | Stream all active refs in `[a, b)` into a sealed recall candidate set |

👉 *For boundary specifications, interval grammar, and worked patterns, see [QUICK_REFERENCE.md](QUICK_REFERENCE.md) and [JOINT_LIMITATIONS.md](JOINT_LIMITATIONS.md).*

---

## Design Philosophy: Constant-Presence Slices

* **Microsecond Segment Decomposition**: Queries decompose time ranges into maximal constant-presence segments, answering point presence, co-presence, concurrency, and temporal gaps in a single linear pass.
* **Ephemeral Arena Allocations**: The query engine allocates zero dynamic memory per match or gap, avoiding malloc overhead during tight server loops.
* **Predictable Boundaries**: Up to 65,536 intervals per database with support for up to 4,096 concurrent overlapping entities.

---

## Installation & Build

### Prebuilt Packages
Prebuilt packages are distributed on tty.pt for Linux (APT / Alpine / Arch / Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD. Follow the [installation instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md) and use **libjoint** as the package name.

### Build from Source
```sh
make                  # builds lib/libjoint.so
make test             # run comprehensive test suite
make bench            # run microsecond query benchmarks
sudo make install     # install headers and library to system
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt. See `LICENSE`.
