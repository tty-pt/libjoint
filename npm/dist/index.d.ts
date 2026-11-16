export declare function scanTime(dateStr: string): number;

export declare class JointTimeline {
  constructor(filePath?: string, options?: { wasm?: boolean });
  static wasm(): JointTimeline;
  start(ref: number, startTs: number): boolean;
  stop(ref: number, endTs: number): boolean;
  recordInterval(ref: number, start: number, end?: number | null): boolean;
  erase(ref: number): boolean;
  storeSpec(ref: number, specStr: string): boolean;
  storeTyped(ref: number, uint8Array: Uint8Array, qtype?: number): boolean;
  queryOverlaps(start: number, end: number): number[];
  count(start?: number, end?: number): number;
  close(): void;
}

export declare class JointTemporalAxis {
  readonly name: string;
  constructor(filePath?: string);
  registerInterval(ref: number, resourceId: number, start: number, end?: number | null): void;
  start(ref: number, startTs: number): boolean;
  stop(ref: number, endTs: number): boolean;
  erase(ref: number): boolean;
  store(ref: number, specStr: string): boolean;
  fill(params: { start: number; end: number; resourceId?: number }): number[];
  close(): void;
}

export interface TimelineState {
  selectedRange: { start: number; end: number } | null;
  conflicts: number[];
  hasConflict: boolean;
}

export interface TimelineController {
  readonly selectedRange: { start: number; end: number } | null;
  readonly conflicts: number[];
  readonly hasConflict: boolean;
  checkRange(start: number, end: number): number[];
  clearRange(): void;
  subscribe(callback: (state: TimelineState) => void): () => void;
}

export declare function createTimelineController(
  timeline: JointTimeline
): TimelineController;
