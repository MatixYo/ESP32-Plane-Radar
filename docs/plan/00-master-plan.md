# Master Plan — Functional-Core Refactor

Status: **awaiting approval.** No source file has been modified. Six read-only
investigators are producing the findings documents this plan depends on
(`findings-01` … `findings-06`); their conclusions may reorder the phases below
but will not change the decisions in §2.

Target branch: `refactor/functional-core` (not yet created).

---

## 1. Measured baseline

Everything here was measured in this session, not carried over from the handoff.

| Fact | Value | How established |
|------|-------|-----------------|
| Host tests | **green**, exit 0 | `make test` |
| `native_test_png` | 26/26 in 0.32 s | suite output |
| PNG fixture generator | runs clean | `python3 scripts/gen_png_fixtures.py` |
| Toolchain | GCC **8.4.0** (`crosstool-NG esp-2021r2-patch5`) | `riscv32-esp-elf-g++ --version` |
| `-std=gnu++20` | **rejected** by toolchain | compile probe |
| Mutable globals | 5, all in `src/main.cpp` | source survey |
| Largest hand-written file | `src/ui/radar_display.cpp`, 696 LOC | `wc -l` |
| Working tree | 7 modified files, matches handoff §8 | `git status --porcelain` |
| Branch | `feat/native-destination`, 5 unpushed commits, 6 ahead of `main` | `git rev-list` |

Frame budget, carried from the handoff and **to be re-verified in P5** rather
than trusted:

| Stage | Cost |
|-------|------|
| Terrain background | 37 ms (was 209 ms before fixed-point) |
| Rings / runways / labels | **61 ms** ← largest remaining target |
| Aircraft | 6 ms |
| Blit to panel | 23 ms (SPI, hardware-bound) |
| **Total** | **128 ms** |

Not visible as flicker — composition is off-screen and one 23 ms blit swaps it
in. The cost is main-loop latency: a BOOT-button tap can be up to ~128 ms late.

---

## 2. Decisions register

Settled with the user before planning. Phases implement these; they are not
reopened during execution.

| # | Decision | Consequence |
|---|----------|-------------|
| D1 | **Pure core, imperative shell.** Not full TCA. | Pure free functions over plain structs; one store mutated in place. Zero heap, zero per-action copies. |
| D2 | **Full Swift-ish naming, method-style APIs.** | `AppState`, `TerrainGrid`, `ViewSpan`, `AircraftPlot`; `grid.elevation(at:)`. `k`-constants stay. `s_`/`g_` prefixes disappear. |
| D2a | **Reconciliation of D1 and D2.** | Value types get `const` **query** methods only — pure by construction. Mutation stays in free functions taking `AppState&`, in the shell. **No mutating member functions.** |
| D3 | **Scope: all of `src/` and `include/`**, except generated `src/core/large_airports_data.cpp`. Includes `src/platform/`. | `radar_display.cpp` (696), `portal_server.cpp` (575), `wifi_setup_device.cpp` (501) are all in. |
| D4 | **Comments: strip from source, rationale moves to `docs/`**, one-line pointer left at the code site. | Governed by the `findings-05` migration table. No RATIONALE comment is deleted without a live destination anchor. |
| D5 | **Persisted format: free rein, including NVS keys.** | Plus a one-shot migration (see D5a). |
| D5a | **Write the NVS migration anyway** (~30 lines). | Existing devices keep their location, sites and preferences. User may overrule. |
| D6 | **Incremental phases, flashable at each gate.** | Every phase compiles, keeps host tests green, and is independently revertible. |
| D7 | **Perf targets: main-loop latency (excl. §9), heap headroom, hot-path float.** | Flash size and build time explicitly deprioritised. |
| D8 | **Handoff §9 dirty-rect optimization: DEFERRED.** | Written up as a costed proposal only. No code. Needs separate go-ahead. |
| D9 | **Git: commit baseline, then branch.** | Baseline commit on `feat/native-destination`; refactor on `refactor/functional-core`. The 5 stacked commits are left as-is. |
| D10 | **Verification: green at every step; user flashes at milestones.** | I run `make test`; user runs `pio run -e supermini -t upload` at each gate. |
| D11 | **Docs: `docs/` tree with `ARCHITECTURE.md` as entry.** | Layout in §4. |
| D12 | **Rewrite the stale `.cursor/` rules and skills.** | `plane-radar-project.mdc` is `alwaysApply: true` and currently misinforms every agent. |

---

## 3. Hard constraints — these bound every phase

Any proposal that violates one of these is rejected regardless of its other
merits. Full explanations land in `docs/constraints.md` during P4.

### 3a. The borrowed-scratch invariant

The frame sprite is 115200 bytes. A terrain tile fetch needs ~30 KB of TLS plus
a decoder — roughly **13 KB more than the heap has**. The PNG decoder therefore
allocates **nothing**: it decodes into scratch borrowed from the frame sprite's
own pixel buffer (`kScratchBytes` = 35840, tied to the `Work` struct by
`static_assert`).

**Invariant: nobody may compose a frame while a tile is decoding.**

Freeing the sprite for the download was tried and **measurably fails** — the
TLS/TCP path strands a few hundred bytes inside the freed 115 KB hole, TCP
`TIME_WAIT` pins them there, and the largest free block comes back as 114676
against the 115200 needed. The sprite never returns and every later frame paints
straight to the panel, with visible progressive redraw. **Do not re-propose it.**

Today the invariant is guarded by a bool (`g_terrain_download_active`) and a
falling-edge repaint. Expressing it in the **type system** instead is an explicit
goal of P3.

### 3b. No FPU

Every float operation is a library call. Per-pixel float math in the terrain
upsample cost 209 ms of a 297 ms frame; the same code in fixed point costs
37 ms. Anything per-pixel or per-aircraft stays integer-only. No abstraction may
reintroduce float, per-pixel indirection, or virtual dispatch on a hot path.

### 3c. gnu++17 is a floor, not a preference — and today it isn't even reached

GCC 8.4.0 rejects `-std=gnu++20`, so C++17 is the ceiling. Two corrections to
what I asserted before the findings landed:

- **Exceptions are ON, not off.** `-fexceptions` is in effect on 157 TUs and
  `CONFIG_COMPILER_CXX_EXCEPTIONS=y`. RTTI *is* off (`-fno-rtti`). Nothing throws
  or catches, and `CONFIG_COMPILER_CXX_EXCEPTIONS_EMG_POOL_SIZE=0` means a
  `throw` under heap exhaustion lands in `std::terminate` — so the design still
  never relies on exceptions, but "`std::variant` won't compile" would have been
  a false claim. See `findings-02` and `findings-04`.
- **`env:supermini` actually compiles at `-std=gnu++11`.** PlatformIO places
  `build_flags` *before* the framework's flags and GCC takes the last `-std`, so
  the framework's `-std=gnu++11` wins over ours. C++17 constructs survive only as
  GNU extensions (e.g. `namespace ui::runway {`). `build_src_flags` is the only
  lever that lands after. Fixing this is a **correctness** item, not a perf one —
  `findings-03` measured `-O2` on `src/` as byte-for-byte neutral.

**No `std::function` on device paths** — it heap-allocates. Effects use POD
structs, enums and plain function pointers. `std::optional`, `std::variant` and
`std::string_view` are available and allocation-free.

---

## 4. Deliverables

```
ARCHITECTURE.md              ← human-facing structure + layer map  (the .md you asked for)
docs/
  constraints.md             ← §3a memory, §3b no-FPU, flashing traps
  modules/terrain.md
  modules/png-decode.md
  modules/render.md
  plan/
    00-master-plan.md        ← this file
    findings-01-bugs.md          ─┐
    findings-02-memory.md         │ produced by the six
    findings-03-performance.md    │ read-only investigators
    findings-04-architecture.md   │ (running now)
    findings-05-comments-docs.md  │
    findings-06-tests.md         ─┘
    09-dirty-rect.md           ← D8 proposal, awaiting go-ahead, no code
```

---

## 4a. What the findings changed

All six investigators reported. Their conclusions revise the plan as follows.

| # | Finding | Effect on the plan |
|---|---|---|
| 1 | **Only 33 % of hand-written firmware is behaviourally pinned.** `src/ui/` (1437 LOC) and `src/platform/` minus the PNG decoder (2408 LOC) have **zero** assertions between them; no test env even compiles most of them. | **P0 grows substantially.** It is no longer a formality — see the revised P0. |
| 2 | **5 of the 26 PNG cases exist only in the working tree.** HEAD has 16 fixtures / 21 cases; the tree has 23 / 26, and the 5 missing ones are the whole Adler-32 family. | The baseline commit is **urgent**, not hygiene. Any agent starting from a clean HEAD silently loses them. |
| 3 | **CI runs only `native_test`** — 40 of 112 offline cases (36 %) never run there, including the entire PNG decoder and the terrain state machine. | Two-line CI fix (`pio test -e native_test` → `make test`, +2.9 s) lands in P0. |
| 4 | **The borrowed-scratch invariant holds today — negative result.** No path composes a frame mid-decode. The mechanism: `pollWifiAndTaps` calls `gesture::tapPress` but never `tapPoll`, so no tap can be *dispatched* from inside `HttpClient::get`. | P3's type-encoding is still worth doing (it makes violation a compile error), but it is **hardening, not a bug fix**. Priority drops slightly. That `tapPress`/`tapPoll` split is load-bearing and undocumented. |
| 5 | **13 bugs found**, one High: a repeat portal save silently and irreversibly overwrites the stored manual lat/lon with the active site's coordinates. Plus 3 `millis() + timeout` rollover sites and a retry gate keyed by preset instead of view. | P1 is real work, not a token phase. |
| 6 | **Comment ratio inverts the premise**: of 1,326 comment lines only ~170 restate the code. ~815 are rationale, ~230 API contract. | P4 is a **migration**, not a cleanup. Two new user decisions (see §9). |
| 7 | **−5,638 bytes of static RAM available** in-tree, and `_heap_start == _bss_end` was verified, so every `.bss` byte freed becomes a heap byte 1:1. | P5's heap target is quantified and real. |
| 8 | **The 61 ms stage is accounted for**: ~20 ms is two anti-aliased crosshair wedge lines, ~19 ms is an airport distance test that runs **1,704×/frame in `double`** because a memo never caches negatives. | P5 has concrete, disassembly-backed targets. 128 ms → ~60 ms without touching §9. |
| 9 | `data::large_airports` **does** exist (`include/core/large_airports.h:7`). I briefed the agents that it didn't. | Correction only; no plan change. |
| 10 | Two wrong facts in the source itself: `settings.cpp:30` says `// 40 NM ring` for the **20 NM** preset, and the decoder size is stated as ~44 KB / ~36 KB / 35,840 B in three places. | Fixed in P4. |

---

## 5. Phase plan

Each phase: parallel agents do the work → I run `make test` → **user flashes** →
phase is committed. Stop after any phase and the device still works.

### P0 — Baseline and safety net
Sequential. No refactoring. **The answer to "how thin is the net?" came back
"33 %", so this phase is now the largest of the low-risk ones — roughly 10 hours
of test writing before any agent touches a source file.**

1. **Commit the 7 modified files** as a green baseline, including the tracked
   `.pyc` deletion (`.gitignore` already lists `__pycache__/`). Urgent: 5 of the
   26 PNG cases and the whole Adler-32 fixture family exist only in the tree.
2. Branch `refactor/functional-core`.
3. **CI fix** (two lines): `pio test -e native_test` → `make test`, plus a
   generator-drift gate (`gen_png_fixtures.py` then `git diff --exit-code`).
   Optionally `make test` in `release.yml` so no tagged release ships red.
4. **Characterisation tests, in `findings-06` §4.7 priority order.** Items 1–5
   need no design decisions and close the cheapest two thirds of the exposure:
   - gap-fill in existing suites — above all `downloadActive()`, which is
     asserted **nowhere** and is the borrowed-scratch guard;
   - `test_portal_params` — 161 LOC of pure logic, currently zero assertions,
     highest value per hour in the document;
   - `test_terrain_upsample` — golden values for the one already-fixed-point hot
     path, which is the template every future conversion will copy and has not a
     single number checked;
   - `test_range_geometry`, `test_portal_http`, `test_boot_button`.
5. Fix `platformio.ini`'s `-std` ordering so `src/` actually compiles as C++17
   (§3c). Correctness, not performance.
6. Two zero-risk cleanups from `findings-04` §1.10: delete the dead
   `s_scale_label_h` / `s_scale_label_max_w`, and wrap the six external-linkage
   `s_boot_*` symbols in `wifi_setup_device.cpp` in its anonymous namespace.
7. Capture `docs/fidelity-baseline.txt` + frame hash **before** anything else —
   it is the only automated guard on the UI phases.
8. Skeleton `ARCHITECTURE.md` + `docs/` tree.

**Gate:** tests green, count strictly higher than baseline (112 → ~160). Flash:
smoke only.
**Two hazards named by `findings-06`:** only `test_settings` sets
`PLANE_RADAR_SETTINGS`, so any new settings-touching suite that forgets it will
read and rewrite the developer's real `~/.plane-radar/settings.json`; and
`test_settings` has a genuine inter-test ordering dependency, so cases must not
be reordered or inserted between the two range tests.

### P1 — Bugs
Parallel, one agent per severity cluster from `findings-01`. Fixes only — no
restructuring, so each fix is a small reviewable diff against unchanged
architecture. Every fix ships with a regression test that fails before it.

**Gate:** tests green. Flash: exercise zoom 6/7/8 and a range change.

### P2 — Pure core extraction
Parallel, one agent per module from the `findings-04` pure/effectful partition.
Computation moves into pure free functions over plain structs; call sites keep
their existing `s_*` state and simply delegate. Naming converts to D2 here.

**Why before P3:** it is mechanical, testable in isolation, and leaves state
ownership untouched — so if P3 is ever abandoned, P2 stands alone as a win.

**Gate:** tests green; new pure functions directly unit-tested. Flash: full
regression.

### P3 — AppState and the imperative shell
The high-risk phase. Largely sequential; parallel only where `findings-04` shows
genuinely independent sub-trees.

1. Introduce `AppState`, absorbing the 5 `main.cpp` globals and the module
   `s_*` statics.
2. `loopTick(AppState&, Clock)` as the shell.
3. **Encode §3a in the type system** — a move-only scratch lease, or a phase
   enum making "composing" and "decoding" mutually exclusive states, so a future
   editor cannot compose a frame mid-decode. Must work without heap or
   exceptions.
4. NVS key rename + the D5a migration.

**Gate:** tests green; migration round-trip proven. **Flash is mandatory here** —
this phase can regress heap headroom, and only the device can show it. Watch for
`SSL - Memory allocation failed`.

### P4 — Comment strip and documentation
Parallel by file group, driven mechanically by the `findings-05` migration table.
Rationale is written to its destination anchor **before** the source comment is
removed, never after. Includes D12: rewriting the four `.cursor/rules` and
auditing the four skills.

**Gate:** tests green (no behaviour change expected). No pointer resolves to a
missing anchor. Flash: smoke only.

### P5 — Performance
Parallel, one agent per D7 target. **Land the per-stage `esp_timer_get_time()`
profiler first** — every millisecond figure in `findings-03` is derived from
instruction counts plus one calibration point, not measured directly.

Ranked by `findings-03` and `findings-02`, all independent of §9:

| Item | Est. | Why |
|---|---|---|
| PERF-03 crosshairs → 6 rect fills | ~20 ms | The two `drawWideLine` calls walk a 1,290-pixel bounding box calling `wedgeLineDistance` (110 instr + 18 soft-float + `sqrtf`) per pixel. The strokes are axis-aligned, so the alpha profile is constant: one opaque column + two half-alpha fringes. Pixel-identical. |
| PERF-02 cache the airport in-range set | ~19 ms | The memo only ever stores `true`, so out-of-range airports re-project **once per runway** — 1,704 `double`-precision distance tests per frame. Also **frees ~2 KB** of `.bss`. |
| PERF-01 local `bandOf` | ~20 ms | 57,600 cross-TU calls per frame into an 8-instruction-per-band linear scan. Confirmed by disassembly that `-O2` cannot fix it — the TU boundary is the barrier. |
| MEM-01/02/06/11 | −4,790 B heap | Zero-risk `.bss` reclamation; ~37 % of the ~13 KB deficit. |
| PERF-04 `Viewport` → `float` | ~2 ms | Six of eleven soft-float calls per projection exist only because the centre is `double` while aircraft coords are already `float`. |
| PERF-11a TLS session reuse | 0.5–2 s **latency** | Not a frame win. A fresh ECDHE handshake per request, uncovered by the poll hook. Three lines. Arguably the largest user-visible improvement in the whole plan. |
| PERF-09 integer `terrariumElevation` | 15–60 ms per download | Eight `double` ops per decoded grid sample; the expression is exactly integer. |

Target: **128 ms → ~60 ms** plus ~5.6 KB of heap, with §9 untouched.

**Gate:** tests green; each change measured before/after, and the native frame
hash byte-identical for everything claimed pixel-identical. Flash: timing and
heap confirmation (`heap_caps_get_largest_free_block`).

---

## 6. Explicitly out of scope

| Item | Why |
|------|-----|
| Handoff §9 dirty-rect optimization | D8 — needs separate go-ahead. Written up in `09-dirty-rect.md`, no code. |
| `src/core/large_airports_data.cpp` | Generated. Changes go through `scripts/build_large_airports.py`. |
| `test/test_png/png_fixtures.h` | Generated by `scripts/gen_png_fixtures.py`. Never hand-edited. |
| Freeing the sprite during download | §3a — tried, measured, fails. |
| Splitting the 5 stacked commits | D9 — left as-is. |
| `-std=gnu++20`, C++20 features | §3c — toolchain rejects it. |
| Flash size, build time | D7 — deprioritised. |

---

## 7. Risk register

| Risk | Severity | Mitigation |
|------|----------|------------|
| P3 regresses heap headroom; failure appears only on device | **High** | Mandatory flash gate; `findings-02` measured baseline to compare against; P3 is revertible independently of P0–P2. |
| Comment strip destroys §3 knowledge | **High** | D4 requires a destination anchor before deletion. `findings-05` must flag comments whose removal it judges dangerous **and may overrule the user's preference** with reasoning. |
| Host tests too thin to protect the refactor | **High** | P0 exists for this. `findings-06` is instructed to be pessimistic; `src/ui/` and `src/platform/` are expected to be thinly covered. |
| Swift-ish method APIs erode the pure/effectful boundary | Medium | D2a: `const` query methods only, no mutating members. Enforced in P2/P3 review. |
| An abstraction silently reintroduces float on a hot path | Medium | §3b; P5 requires disassembly evidence (`__addsf3`, `__mulsf3`, `__divdf3`, …). |
| Parallel agents conflict on the same files | Medium | Phases partition by module, not by concern; a file has one owner per phase. |
| NVS rename loses user settings | Medium | D5a migration + round-trip test in P0. |
| Handoff frame numbers are stale | Low | P5 re-measures rather than trusting them. |

---

## 8. Verification protocol

I can run, every phase:

```
make test          # 3 offline envs — must be green before any phase closes
make test-build    # compile-only
pio run -e supermini
python3 scripts/gen_png_fixtures.py   # must stay clean; regenerates fixtures
```

**Only the user can run**, at each gate:

```
pio run -e supermini -t upload
```

Flashing traps, all pre-existing: uploads use `--no-stub` at 115200 because
esptool's stub dies on the C3's USB-Serial-JTAG; **the serial monitor must be
closed** or the upload fails; uploads occasionally die mid-write with "Device not
configured" — retry or replug.

`make test-live` (3 cases, real AWS tiles) is opt-in and needs internet. Run it
at the P1 and P3 gates.

---

## 9. Open items

| # | Item | Needs | My recommendation |
|---|------|-------|-------------------|
| 1 | Approve this plan and start P0 | User | — |
| 2 | **Docs layout deviation.** ~210 rationale lines fit none of the five agreed files (native harness ~155, config portal ~130). Add `docs/modules/native-harness.md` + `portal.md`, or fold both into `ARCHITECTURE.md`? | User | Add the two files. Folding makes `ARCHITECTURE.md` ~600 lines and it stops being a structure doc. Anchors are pre-namespaced either way. |
| 3 | **The 25 inline-comment exceptions.** `findings-05` §7 pushes back on D4 at 25 sites (~38 lines total, 0.5 % of the tree) where removing the comment makes a *silent* failure likely. | User adjudicates | Accept, under the stated rule: ≤2 lines, must end in a `docs/` anchor, must name a **failure** not a mechanism. That rule admits these 25 and almost nothing else. |
| 4 | §9 dirty-rect go-ahead (D8) | User, after `09-dirty-rect.md` | Deferred as agreed. Note P5 already gets 128 → ~60 ms without it. |
| 5 | Keep or drop the NVS migration (D5a) | User | Keep it (~30 lines). `findings-06` found five migration traps that make a hand-waved rename genuinely dangerous — including that `kNsLocation == "radar"` and `kNsRadar == "planeradar"`, i.e. the constant names are **inverted** relative to their literals. |
| 6 | **`kDisplayRgbOrder` TODO** (`config.h:43-50`) — needs a board on a desk to confirm whether red renders red with the R/B swap removed. It is the repo's only TODO and it makes one constant drive two mechanisms that could disagree. | **User, physically** | Close it before P4; it removes 8 comment lines and a real hazard in minutes. |
| 7 | **MEM-03 / MEM-12** (−14 KB TLS heap, −5.7 KB coredump) require moving off the pinned `espressif32@6.5.0` Arduino path to an Arduino-as-IDF-component build. | User | **Not now.** Together they'd close the entire 13 KB deficit, but they change every prebuilt library and invalidate the fidelity baseline mid-refactor. Revisit after P5. |
| 8 | **MEM-08** `kMaxAircraft` 64 → 40 (−1,152 B) is a product judgement — at an 80 NM ring over a dense metro the surplus silently vanishes. | User | Log the existing `adsb: %u aircraft` line over a busy period first; decide on data. |
| 9 | **Two visual changes** in P5: PERF-05 drops the ICAO label from 3 draws to 1 (thinner label), PERF-06 optionally replaces the speed-vector wedge with two Bresenham lines (loses anti-aliasing on diagonals). | User | Take PERF-05; hold PERF-06's line change and keep only its trig de-duplication unless you want the extra ms. |
