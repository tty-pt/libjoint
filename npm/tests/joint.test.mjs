import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { JointTimeline, JointTemporalAxis, scanTime, createTimelineController } from "../dist/index.js";

test("JointTimeline record and overlap intersection", () => {
  const tmpDir = path.join(os.tmpdir(), `joint_npm_test_${Date.now()}`);
  fs.mkdirSync(tmpDir, { recursive: true });
  const jointFile = path.join(tmpDir, "test.joint");

  const timeline = new JointTimeline(jointFile);
  assert.ok(timeline, "JointTimeline should initialize");

  // Interval A: 1000 -> 2000 for Entity 101
  timeline.recordInterval(101, 1000, 2000);

  // Interval B: 3000 -> 4000 for Entity 102
  timeline.recordInterval(102, 3000, 4000);

  // Interval C: 1500 -> 3500 for Entity 103
  timeline.recordInterval(103, 1500, 3500);

  // Query 1: 1200 -> 1800 should overlap 101 and 103
  const q1 = timeline.queryOverlaps(1200, 1800);
  assert.ok(q1.includes(101), "Should include 101");
  assert.ok(q1.includes(103), "Should include 103");
  assert.ok(!q1.includes(102), "Should NOT include 102");

  timeline.close();
  fs.rmSync(tmpDir, { recursive: true, force: true });
});

test("JointTimeline open-ended start/stop, erase, and scanTime", () => {
  const timeline = new JointTimeline();
  timeline.start(501, 1000); // open-ended [1000, +inf)
  assert.deepEqual(timeline.queryOverlaps(5000, 6000), [501]);

  timeline.stop(501, 2000); // closed [1000, 2000)
  assert.deepEqual(timeline.queryOverlaps(5000, 6000), []);
  assert.deepEqual(timeline.queryOverlaps(1200, 1800), [501]);

  assert.equal(timeline.erase(501), true);
  assert.deepEqual(timeline.queryOverlaps(1200, 1800), []);

  const ts = scanTime("2025-01-01T00:00:00");
  assert.ok(ts > 1700000000);
  timeline.close();
});

test("JointTemporalAxis store grammar and erase", () => {
  const axis = new JointTemporalAxis();
  assert.equal(axis.store(77, "100,300"), true);
  assert.deepEqual(axis.fill({ start: 150, end: 250 }), [77]);
  assert.equal(axis.erase(77), true);
  assert.deepEqual(axis.fill({ start: 150, end: 250 }), []);
  axis.close();
});

test("createTimelineController provides reactive interval conflict tracking", () => {
  const timeline = new JointTimeline();
  timeline.recordInterval(201, 1000, 2000);

  const controller = createTimelineController(timeline);
  assert.equal(controller.hasConflict, false);

  let updates = [];
  const unsubscribe = controller.subscribe((state) => {
    updates.push(state);
  });

  const conflicts = controller.checkRange(1500, 2500);
  assert.equal(conflicts.length, 1);
  assert.equal(controller.hasConflict, true);
  assert.ok(controller.conflicts.includes(201));

  controller.clearRange();
  assert.equal(controller.hasConflict, false);

  unsubscribe();
  timeline.close();
});

test("withOverlaps zero-copy raw memory pointer query", () => {
  const tmpFile = path.join(os.tmpdir(), `test_overlap_${Date.now()}.joint`);
  const timeline = new JointTimeline(tmpFile);
  timeline.recordInterval(301, 1000, 2000);
  timeline.recordInterval(302, 1500, 2500);

  timeline.withOverlaps(1200, 1800, (ptr, count) => {
    assert.ok(count >= 2);
    assert.ok(ptr > 0);
  });

  assert.ok(timeline.count(1200, 1800) >= 2);
  const raw = timeline.rawPointer(1200, 1800);
  assert.ok(raw.ptr > 0);
  assert.ok(raw.count >= 2);
  raw.free();

  timeline.close();
  try { fs.unlinkSync(tmpFile); } catch {}
});
