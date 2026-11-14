# Changelog

All notable changes to libjoint will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.8.0] - 2026-11-14

- **Complete Node-API Bindings & Open-Ended Intervals (`npm/native/joint_napi.c`, `npm/dist/index.js`)**:
  - Bound `start` (`joint_start`), `stop` (`joint_stop`), `erase` (`joint_erase`), `scanTime` (`sscantime`), and `axisStore`/`axisStoreTyped`/`axisUnstore` in `joint_napi.c`.
  - Removed JS `resourceMapping` fallback in `JointTimeline` and added open-ended interval support.
  - Requires `@tty-pt/corm` >=2.4.0 (`^2.4.0`) and `@tty-pt/stele` >=1.7.0 (`^1.7.0`).

## [1.7.3] - 2026-11-13

- **Packaging**:
  - Requires `@tty-pt/corm` >=2.3.3 (`^2.3.3`) and `@tty-pt/stele` >=1.6.2 (`^1.6.2`).
  - The npm tarball is built once by CI, with a self-contained native addon per platform (`prebuilds/<os>-<cpu>/`) next to `joint.wasm`, and published as verified.

## [1.7.2] - 2026-11-13

- **Fix (`JointTimeline`, WebAssembly mode)**: the interval buffer is cleared when a timeline is created or closed, so one timeline no longer sees another's intervals.

## [1.7.1] - 2026-11-13

- **Disjoint Global-Base & Shared-Memory Reactor (`joint.wasm`)**:
  - Rebuilt `joint.wasm` shared-memory reactor at a disjoint `--global-base=786432` to prevent static data segment collisions across co-loaded WASM modules.
  - Upgraded `@tty-pt/corm` to `>=2.3.2` (`^2.3.2`) and `@tty-pt/stele` to `>=1.6.1` (`^1.6.1`).

## [1.7.0] - 2026-11-13

- **Substrate & Corm v2.3.1 Alignment**:
  - Upgraded dependencies to @tty-pt/corm@^2.3.1 (>=2.3.1) and @tty-pt/stele@^1.6.0 (>=1.6.0).
  - Maintained zero-copy linear memory candidate chaining across CandidateSet and JointTimeline.

## [1.6.3] - 2026-11-03

- **Zero-Copy Shared Memory Chaining (`JointTimeline`) & Substrate Integration**:
  - Added `.count(start, end)`, `.rawPointer(start, end)` returning `{ ptr, count, free }`, and `.withOverlaps(start, end, fn)`.
  - Enables zero-copy raw memory pointer queries across shared WebAssembly linear memory (`-Wl,--import-memory`), directly seeding `@tty-pt/corm`'s `CandidateSet` and rankers.
  - Adopted strict `peerDependencies` (`@tty-pt/stele: ">=1.5.0"`, `@tty-pt/corm: ">=2.2.0"`) and `devDependencies` (`@tty-pt/stele: "^1.5.2"`, `@tty-pt/corm: "^2.2.4"`).
- **Standalone `@tty-pt/joint` Package & Centralized Build**:
  - Published `@tty-pt/joint` providing Node-API native bindings and pure WebAssembly fallback (`joint.wasm`).
  - Added `npm/wasm/joint_wasm.c` using `ISO_EXPORT` for automatic symbol export.
  - Centralized WASM compilation in `external/mk/include.mk` (`make wasm`) with post-link `wasm-opt -O3` optimization (dropping binary size from 45 KB to 36 KB).
- **HTOK ISO Date Ingestion & Packaging Purity**:
  - Exported `rec_axis_store_typed` allowing direct ingestion of ISO date strings and epoch intervals from `libhyle-source`'s `.tok` binary containers into the interval index.
  - Explicitly cleared `INSTALL_BIN :=` in `Makefile` preventing internal test binaries from conflicting with `/usr/bin/test`.

## [1.4.0]

- **Cormr Recall Kernel Integration**: Updated to consume `<ttypt/corm.h>` and `<ttypt/cormr.h>`, aligning with the domain-free Recall Kernel architecture.
- **Lemma Text Indexing Alignment**: Updated ecosystem references and test fixtures from `libstoma` to `liblemma`.
- **Memory Footprint Optimization**: Scaled down initial allocation footprints for interval and split buffers.

## [1.3.0]

- **Renamed `libit` → `libjoint`**: the `it_*` API is now `joint_*` (`include/ttypt/joint.h`, `it.pc` → `joint.pc`; `LIBIT_LIMITATIONS.md` → `JOINT_LIMITATIONS.md`). Renames span the whole walker/splitter read model while the interval-tree semantics stay intact.
- **Kernel composition**: new `rec_axis_*` adapters (`rec_axis_fill_interval`, `rec_axis_open`, plus `rec_axis_store`/`rec_axis_unstore`/`rec_axis_readback`) plug joint's interval/overlap matching straight into the recall kernel with `rec_ref_t` (`uint32_t`) refs; all axis parameters are CLI parameters.
- **Sweep-line performance tiers**: public API unchanged; all work is in the query/split path (`joint_iter` → `joint_next`) — `-O3 -mpopcnt -mavx2 -mfma` build (was `-O0`), pointer-cast split compares (no 16-byte `memcpy` per comparison), a query-level `match_arena` replacing per-match mallocs, a block-list `split_arena` replacing per-split mallocs, and an ephemeral `who_set` (open addressing) replacing the per-gap temp corm. Query benchmarks: 1k overlap query 48.5×, 3k overlap query 48×, 1000 insert/query cycles 6.8×, sparse query 6.2×, splits(100) 5.5× faster (medians; `make bench`).
- **Bug fixes**: two latent signed-integer overflows in `src/test_extended.c` exposed by UBSan (`int` overflow in timestamp arithmetic, `int64` overflow in query-end) — both corrected to `time_t`/`huge` arithmetic.
- `libqmap` → `libcorm` rename.
