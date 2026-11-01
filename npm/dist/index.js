import { createRequire } from "node:module";
import path from "node:path";

let nativeBinding = null;
try {
  const req = createRequire(import.meta.url);
  try {
    nativeBinding = req("../build/Release/joint_node.node");
  } catch {}
  if (!nativeBinding && typeof process !== "undefined" && process.platform && process.arch) {
    try {
      nativeBinding = req(`@ttypt/joint-${process.platform}-${process.arch}`);
    } catch {}
  }
  if (!nativeBinding) {
    try {
      nativeBinding = req(path.join(process.cwd(), "build/Release/joint_node.node"));
    } catch {}
  }
} catch {
  // Native addon not available; in-memory / WASM fallback
}

function getSharedWasm() {
  if (globalThis.__ttypt_shared_wasm__) {
    return globalThis.__ttypt_shared_wasm__;
  }
  try {
    const req = createRequire(import.meta.url);
    const qsysPaths = [
      path.join(path.dirname(import.meta.url.replace("file://", "")), "../../libqsys/npm/dist/index.js"),
      path.join(process.cwd(), "node_modules/@ttypt/qsys/dist/index.js"),
      "@ttypt/qsys"
    ];
    for (const p of qsysPaths) {
      try {
        const mod = req(p);
        if (mod && mod.getSharedWasm) {
          return mod.getSharedWasm();
        }
      } catch {}
    }
  } catch {}
  return null;
}

/**
 * JointTimeline: Interval-Tree Temporal Presence & Conflict Engine
 */
export class JointTimeline {
  constructor(filePath = "") {
    this.filePath = filePath;
    this.handle = null;
    this._memIntervals = [];

    if (nativeBinding) {
      this.handle = nativeBinding.init(this.filePath);
    }
  }

  recordInterval(ref, start, end) {
    if (nativeBinding && this.handle) {
      nativeBinding.record(this.handle, ref, start, end);
    } else {
      const wasm = getSharedWasm();
      if (wasm) {
        wasm.joint_wasm_record(ref, BigInt(start), BigInt(end));
      } else {
        this._memIntervals.push({ ref, start, end });
      }
    }
  }

  queryOverlaps(start, end) {
    if (nativeBinding && this.handle) {
      return nativeBinding.intersect(this.handle, start, end);
    }
    const wasm = getSharedWasm();
    if (wasm) {
      const outPtr = wasm.malloc(4096);
      const hits = wasm.joint_wasm_intersect(BigInt(start), BigInt(end), outPtr, 1024);
      const arr = new Uint32Array(wasm.memory.buffer, outPtr, hits);
      const res = Array.from(arr);
      wasm.free(outPtr);
      return res;
    }
    const matching = [];
    for (const item of this._memIntervals) {
      // Overlap condition: start < item.end && end > item.start
      if (start < item.end && end > item.start) {
        if (!matching.includes(item.ref)) matching.push(item.ref);
      }
    }
    return matching;
  }

  close() {
    if (nativeBinding && this.handle) {
      nativeBinding.close(this.handle);
      this.handle = null;
    }
    this._memIntervals = [];
  }
}

/**
 * Pluggable CormAxis implementation for the Corm Recall Kernel
 */
export class JointTemporalAxis {
  constructor(filePath = "") {
    this.name = "temporal";
    this.timeline = new JointTimeline(filePath);
    this.resourceMapping = new Map();
  }

  registerInterval(ref, resourceId, start, end) {
    this.resourceMapping.set(ref, resourceId);
    this.timeline.recordInterval(ref, start, end);
  }

  fill(params) {
    const hits = this.timeline.queryOverlaps(params.start, params.end);
    const matching = [];
    for (const ref of hits) {
      if (params.resourceId !== undefined) {
        const res = this.resourceMapping.get(ref);
        if (res !== undefined && res !== params.resourceId) continue;
      }
      matching.push(ref);
    }
    return matching;
  }

  close() {
    this.timeline.close();
  }
}

/**
 * Framework-agnostic reactive timeline controller for per-frame 60/120 FPS collision feedback.
 * Zero external framework dependencies.
 */
export function createTimelineController(timeline) {
  let selectedRange = null;
  let conflicts = [];
  const subscribers = new Set();

  function notify() {
    const state = {
      selectedRange,
      conflicts: [...conflicts],
      hasConflict: conflicts.length > 0
    };
    for (const sub of subscribers) {
      try {
        sub(state);
      } catch (err) {
        console.error("Timeline subscriber error:", err);
      }
    }
  }

  return {
    get selectedRange() {
      return selectedRange;
    },
    get conflicts() {
      return [...conflicts];
    },
    get hasConflict() {
      return conflicts.length > 0;
    },
    checkRange(start, end) {
      selectedRange = { start, end };
      conflicts = timeline.queryOverlaps(start, end);
      notify();
      return conflicts;
    },
    clearRange() {
      selectedRange = null;
      conflicts = [];
      notify();
    },
    subscribe(callback) {
      subscribers.add(callback);
      callback({
        selectedRange,
        conflicts: [...conflicts],
        hasConflict: conflicts.length > 0
      });
      return () => {
        subscribers.delete(callback);
      };
    }
  };
}
