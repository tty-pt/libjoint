export declare class JointTimeline {
  constructor(filePath?: string);
  recordInterval(ref: number, start: number, end: number): void;
  queryOverlaps(start: number, end: number): number[];
  close(): void;
}

export declare class JointTemporalAxis {
  readonly name: string;
  constructor(filePath?: string);
  registerInterval(ref: number, resourceId: number, start: number, end: number): void;
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
