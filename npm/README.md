<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/libjoint/main/assets/logo.png" alt="@tty-pt/joint logo" width="220" />
</p>

# @tty-pt/joint

[![npm version](https://img.shields.io/badge/npm-v1.8.1-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-Reactor-654FF0?logo=webassembly)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`libjoint`**, the high-performance C99 interval-tree temporal presence engine. Features real-time calendar conflict detection, interval intersections, and reactive timeline controllers. Canonical C header: `<ttypt/joint.h>`.

📖 **Full C & Temporal Engine Documentation**: See the main [**libjoint GitHub README**](https://github.com/tty-pt/libjoint#readme) for interval-tree presence transitions (`joint_start`, `joint_stop`), constant-presence slice iteration, and C API details.

---

## 1. Quickstart & Installation

```bash
npm install @tty-pt/joint
```

**Native OS Packages (`tty.pt`)**: Prebuilt native C packages (`libjoint`) for Linux (`apt`, `apk`, `pacman`, `dnf`), macOS (`brew`), Windows (`winget`, MSYS2), and OpenBSD (`pkg_add`) are available via the [**tty.pt package repositories**](https://github.com/tty-pt/ci/blob/main/docs/install.md).

```typescript
import { JointTimeline, createTimelineController } from "@tty-pt/joint";

// 1. Initialize timeline (file-backed or in-memory)
const timeline = new JointTimeline("./schedule.joint");

// 2. Record presence / booking intervals (entityId, startUnix, endUnix)
timeline.recordInterval(101, 1793400000, 1793428800);
timeline.recordInterval(102, 1793410000, 1793440000);

// 3. Query overlapping entities across a time window
const overlaps = timeline.queryOverlaps(1793405000, 1793415000);
console.log("Conflicting entities:", overlaps); // [101, 102]

timeline.close();
```

---

## 2. Framework-Agnostic Reactive Timeline Controller

Provides 60/120 FPS real-time conflict tracking for interactive UI drag-and-drop calendars without any framework dependencies (works with vanilla DOM, React, Svelte, Vue):

```typescript
import { JointTimeline, createTimelineController } from "@tty-pt/joint";

const timeline = new JointTimeline();
timeline.recordInterval(201, 1000, 2000);

const controller = createTimelineController(timeline);

// Subscribe to conflict changes during slider drag
const unsubscribe = controller.subscribe(({ hasConflict, conflicts }) => {
  badge.innerText = hasConflict ? `Conflict with ${conflicts.join(", ")}` : "Available";
  badge.className = hasConflict ? "text-red-500" : "text-green-500";
});

// Update range on user interaction:
slider.addEventListener("input", (e) => {
  controller.checkRange(startTime, endTime);
});
```

---

## 3. JavaScript / TypeScript API Reference

### `JointTimeline`
C99 Interval-tree temporal presence engine wrapping `joint_node.node` / `joint.wasm`.
* `constructor(filePath?: string)`: Initializes file-backed database or in-memory timeline.
* `.start(ref: number, ts: number): void` / `.stop(ref: number, ts: number): void`: Records open-ended or closed presence transitions (`joint_start` / `joint_stop`).
* `.recordInterval(ref: number, start: number, end: number): void`: Records half-open temporal interval $[start, end)$ for entity `ref`.
* `.queryOverlaps(start: number, end: number): number[]`: Returns array of entity reference IDs overlapping the query window. Strictly executes in compiled WebAssembly / C without JavaScript loops.
* `.scanTime(start: number, end: number): Array<{ min: number, max: number, count: number, who: number }>`: Iterates constant-presence slices (`joint_iter` / `joint_next`).
* `.clear(): void`: Clears all registered intervals (`joint_erase`).
* `.close(): void`: Flushes changes and closes database handle.

### `createTimelineController(timeline: JointTimeline)`
Factory providing reactive state management for per-frame 60/120 FPS calendar sliders:
* `hasConflict: boolean`: Current boolean conflict state.
* `conflicts: number[]`: Array of conflicting entity reference IDs.
* `.subscribe(fn: (state) => void): () => void`: Registers listener and returns unsubscribe function.
* `.checkRange(start: number, end: number): number[]`: Recomputes overlaps for range and notifies subscribers.
* `.clearRange(): void`: Clears range and resets conflict state.

### `JointTemporalAxis`
Plug-and-play CormAxis adapter for the Corm Recall Kernel (`@tty-pt/corm`):
* `.registerInterval(ref: number, resourceId: number, start: number, end: number): void`
* `.store(ref: number, spec: string): boolean` / `.storeTyped(ref: number, data: Uint8Array | Buffer, qtype?: number): boolean`
* `.unstore(ref: number): boolean`
* `.fill(params: { start: number, end: number, resourceId?: number }): number[]`

### `getJointWasm(): WebAssembly.Instance`
Returns the WebAssembly instance loaded on the Stele shared-memory substrate.

---

## 4. Dual-Engine Runtime

- **Node.js**: Uses high-speed native C N-API addon (`build/Release/joint_node.node`) with persistent map indexing.
- **Browser & Edge**: Runs pre-compiled `dist/joint.wasm` reactor with zero native compilation prerequisites.

---

## 5. C Development Headers & Native Integration

```makefile
CFLAGS += -I./node_modules/@tty-pt/joint/include
LDLIBS += -ljoint
```

---

## License

BSD-2-Clause — Copyright (c) 2026, Paulo Quirino <quirinpa@gmail.com>. See [LICENSE](https://github.com/tty-pt/libjoint/blob/main/LICENSE).
