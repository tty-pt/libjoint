<p align="center">
  <img src="https://raw.githubusercontent.com/tty-pt/libjoint/main/assets/logo.png" alt="@ttypt/joint logo" width="220" />
</p>

# @ttypt/joint

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
npm install @ttypt/joint
```

```typescript
import { JointTimeline, createTimelineController } from "@ttypt/joint";

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
import { JointTimeline, createTimelineController } from "@ttypt/joint";

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

## 3. Dual-Engine Runtime

- **Node.js**: Uses high-speed native C N-API addon (`build/Release/joint_node.node`) with persistent map indexing.
- **Browser & Edge**: Runs pre-compiled `dist/joint.wasm` reactor with zero native compilation prerequisites.

---

## 4. C Development Headers via NPM

```makefile
# Include in your C Makefile:
CFLAGS += -I./node_modules/@ttypt/joint/include
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
