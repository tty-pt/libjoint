import { loadWasmModule, loadNativeAddon, getSubstrate } from "@tty-pt/stele";

const nativeBinding = loadNativeAddon(import.meta.url, "joint");
let wasmInstance = null;

export function getJointWasm() {
  if (wasmInstance) return wasmInstance;
  wasmInstance = loadWasmModule(new URL("./joint.wasm", import.meta.url));
  return wasmInstance;
}

/**
 * JointTimeline: Interval-Tree Temporal Presence & Conflict Engine
 */
export class JointTimeline {
  constructor(filePath = "") {
    this.filePath = filePath;
    this.handle = null;

    if (nativeBinding && nativeBinding.init) {
      this.handle = nativeBinding.init(this.filePath);
    }
  }

  recordInterval(ref, start, end) {
    if (nativeBinding && this.handle) {
      nativeBinding.record(this.handle, ref, start, end);
    } else {
      const wasm = getJointWasm();
      wasm.exports.joint_wasm_record(ref, BigInt(start), BigInt(end));
    }
  }

  queryOverlaps(start, end) {
    if (nativeBinding && this.handle) {
      return nativeBinding.intersect(this.handle, start, end);
    }
    const wasm = getJointWasm();
    const outPtr = wasm.exports.malloc(4096);
    const hits = wasm.exports.joint_wasm_intersect(BigInt(start), BigInt(end), outPtr, 1024);
    const mem = getSubstrate().memory.buffer;
    const arr = new Uint32Array(mem, outPtr, hits);
    const res = Array.from(arr);
    wasm.exports.free(outPtr);
    return res;
  }

  /**
   * Returns count of matching intervals for a time range
   * @param {number|bigint} [start]
   * @param {number|bigint} [end]
   * @returns {number}
   */
  count(start = 0, end = Number.MAX_SAFE_INTEGER) {
    if (nativeBinding && this.handle) {
      return this.queryOverlaps(start, end).length;
    }
    const wasm = getJointWasm();
    const outPtr = wasm.exports.malloc(4096);
    const hits = wasm.exports.joint_wasm_intersect(BigInt(start), BigInt(end), outPtr, 1024);
    wasm.exports.free(outPtr);
    return Number(hits);
  }

  /**
   * Returns a raw WASM memory pointer containing matching interval entity IDs.
   * Allocates buffer in shared WASM memory and returns { ptr, count, free() }.
   * @param {number|bigint} [start]
   * @param {number|bigint} [end]
   * @returns {{ ptr: number, count: number, free: () => void }}
   */
  rawPointer(start = 0, end = Number.MAX_SAFE_INTEGER) {
    const wasm = getJointWasm();
    if (nativeBinding && this.handle) {
      const hits = this.queryOverlaps(start, end);
      const ptr = wasm.exports.malloc(Math.max(hits.length * 4, 64));
      const mem = getSubstrate().memory.buffer;
      new Uint32Array(mem, ptr, hits.length).set(hits);
      return {
        ptr,
        count: hits.length,
        free: () => wasm.exports.free(ptr)
      };
    }
    const ptr = wasm.exports.malloc(4096);
    const hits = Number(wasm.exports.joint_wasm_intersect(BigInt(start), BigInt(end), ptr, 1024));
    return {
      ptr,
      count: hits,
      free: () => wasm.exports.free(ptr)
    };
  }

  /**
   * Zero-copy query: executes callback with raw WASM memory pointer and hit count
   * directly from shared linear memory without JS array allocations.
   * @param {number|bigint} start
   * @param {number|bigint} end
   * @param {(ptr: number, count: number) => any} fn
   * @returns {any}
   */
  withOverlaps(start, end, fn) {
    if (nativeBinding && this.handle) {
      const hits = this.queryOverlaps(start, end);
      const wasm = getJointWasm();
      const outPtr = wasm.exports.malloc(Math.max(hits.length * 4, 64));
      const mem = getSubstrate().memory.buffer;
      new Uint32Array(mem, outPtr, hits.length).set(hits);
      try {
        return fn(outPtr, hits.length);
      } finally {
        wasm.exports.free(outPtr);
      }
    }
    const wasm = getJointWasm();
    const outPtr = wasm.exports.malloc(4096);
    const hits = wasm.exports.joint_wasm_intersect(BigInt(start), BigInt(end), outPtr, 1024);
    try {
      return fn(outPtr, hits);
    } finally {
      wasm.exports.free(outPtr);
    }
  }

  clear() {
    if (nativeBinding && this.handle) {
      if (nativeBinding.clear) nativeBinding.clear(this.handle);
    } else {
      const wasm = getJointWasm();
      wasm.exports.joint_wasm_clear();
    }
  }

  close() {
    if (nativeBinding && this.handle) {
      nativeBinding.close(this.handle);
      this.handle = null;
    }
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
 * Factory helper for reactive timeline controller
 */
export function createTimelineController(timeline) {
  let hasConflict = false;
  let conflicts = [];
  const listeners = new Set();

  return {
    get hasConflict() {
      return hasConflict;
    },
    get conflicts() {
      return conflicts;
    },
    subscribe(fn) {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    checkRange(start, end) {
      conflicts = timeline.queryOverlaps(start, end);
      hasConflict = conflicts.length > 0;
      for (const fn of listeners) fn({ hasConflict, conflicts });
      return conflicts;
    },
    clearRange() {
      conflicts = [];
      hasConflict = false;
      for (const fn of listeners) fn({ hasConflict, conflicts });
    }
  };
}
