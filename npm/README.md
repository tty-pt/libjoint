<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/libjoint/main/assets/logo.png" alt="@tty-pt/joint logo" width="220" />
</p>

# @tty-pt/joint

[![npm version](https://img.shields.io/badge/npm-v0.1.0-CB3837?logo=npm)](#)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![TypeScript Ready](https://img.shields.io/badge/TypeScript-Ready-3178C6?logo=typescript)](#)
[![Node.js Native](https://img.shields.io/badge/Node.js-N--API-339933?logo=node.js)](#)
[![WebAssembly](https://img.shields.io/badge/WebAssembly-Reactor-654FF0?logo=webassembly)](#)
[![Zero Dependencies](https://img.shields.io/badge/dependencies-0-success)](#)

> Universal Node.js and Browser bindings for **`libjoint`**, the high-performance C99 interval-tree temporal presence engine. Features real-time calendar conflict detection, interval intersections, and reactive timeline controllers.

---

## 1. Quickstart

```bash
npm install @tty-pt/joint
```

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
* `.recordInterval(ref: number, start: number, end: number): void`: Records half-open temporal interval $[start, end)$ for entity `ref`.
* `.queryOverlaps(start: number, end: number): number[]`: Returns array of entity reference IDs overlapping the query window. Strictly executes in compiled WebAssembly / C without JavaScript loops.
* `.clear(): void`: Clears all registered intervals.
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
* `.fill(params: { start: number, end: number, resourceId?: number }): number[]`

### `getJointWasm(): WebAssembly.Instance`
Returns the WebAssembly instance loaded on the Stele shared-memory substrate.

---

## 4. Dual-Engine Runtime

- **Node.js**: Uses high-speed native C N-API addon (`build/Release/joint_node.node`) with persistent map indexing.
- **Browser & Edge**: Runs pre-compiled `dist/joint.wasm` reactor with zero native compilation prerequisites.

---

## 4. C Development Headers via NPM

```makefile
# Include in your C Makefile:
CFLAGS += -I./node_modules/@tty-pt/joint/include
LDLIBS += -ljoint
```

---

## 5. Building from Source

```bash
make npm-build    # Compiles native addon and WASM reactor
make npm-test     # Runs verification test suite
```

## License

BSD 2-Clause License. Copyright (c) 2026, tty-pt.
