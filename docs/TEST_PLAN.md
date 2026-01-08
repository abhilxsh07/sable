# Reader test plan

Started: 19 October 2026. Target: the current local alpha source.

I want a repeatable record of what works, what fails and what still needs a
real device. Passing this list reduces risk; it cannot prove the absence of
bugs. Previous results are useful baselines, but are not fresh passes here.

## Working plan

- Goal: find crashes, data loss, rendering faults and reader regressions
  before calling this build ready for wider use.
- Read: reader checks, build/test scripts, native probes, and the code behind
  each failure. Start with the known printing lifetime failure.
- Change: fix confirmed defects within the current reader features, add
  regression checks where needed, and keep this record current.
- Steps: inventory, reproduce, isolate, fix, retest, run the full matrix,
  then verify portable packages from the final source.
- Verify: retain commands, exit codes, counts and failure logs. Test both
  Debug and Release; compile both architectures. Use bounded stress runs.
- Risks: this host cannot stand in for a physical trackpad, another GPU,
  a clean Windows installation or an ARM64 machine. Do not send jobs to
  physical printers or change the owner's files/settings for testing.

## Status key

Current result: 86 check groups, with 65 Pass, 9 Partial, 11 Manual and
1 Blocked. There are no queued checks left in this local pass. Partial and
Manual rows remain open for the specific input, device or duration named.

Pending = queued; Running = in progress; Pass = checked in this pass;
Fail = reproduced; Blocked = the environment prevents the check;
Manual = needs the owner's input or another device. Partial records the
covered part and explicitly names the missing part. Each row is a group
of related checks, not a claim that every possible input has been tested.

## Checklist

| ID | What I will try | Method | Status |
|---|---|---|---|
| B01 | x64 Debug build, warnings treated as errors | Build | Pass: 0 warnings |
| B02 | x64 Release with normal link-time optimization | Build | Pass: LTO, 0 warnings |
| B03 | ARM64 Debug build | Build | Pass: 0 warnings |
| B04 | ARM64 Release with normal link-time optimization | Build | Pass: LTO, 0 warnings |
| B05 | C++ formatting and whitespace errors | Static | Pass: all 150 C++ files and diff whitespace |
| B06 | Complete Debug test suite, including printing | Unit/integration | Pass: 453 passed, 1 hard-link skip |
| B07 | Complete Release test suite, including printing | Unit/integration | Pass: 453 passed, 1 hard-link skip |
| B08 | Repeated shuffled suites, fixed recorded seeds | Stress | Pass: 5 Debug, 5 Release and 3 ASan full-suite repetitions |
| F01 | Valid PDFs, missing files, empty files, non-PDFs, damaged PDFs | Unit/integration | Pass: ordinary fixtures plus 192 damaged inputs |
| F02 | Password failure, retry, cancellation and closed tabs | Engine/native | Pass: engine and native lifecycle; physical typing in N08 |
| F03 | Unicode and spaced paths; loaded bytes survive source changes | Unit/integration | Pass |
| F04 | Restricted documents preserve copy/print permissions | Unit/integration | Pass |
| F05 | Mixed page sizes, crop boxes, rotations, page labels | Unit/native | Pass |
| F06 | Long documents; first, middle and final pages | Engine/native | Pass: includes 1,000-page native regression |
| R01 | Raster bounds, invalid dimensions and failed renders | Unit/integration | Pass |
| R02 | Filter golden colors; original colors in thumbnails and print | Unit/GPU | Pass |
| R03 | Tile boundaries, partial redraws and visible page coverage | Unit/native/visual | Partial: tile/coverage checks and sampled captures pass; reported seam needs physical retest |
| R04 | Cache budget, eviction, stale work, rerender after cache clear | Unit/native | Pass |
| R05 | Surface recreation, simulated device loss and scale changes | Unit/native | Pass: simulated draw loss, native Error and Scale; actual device loss in R06 |
| R06 | Actual GPU reset, remote desktop and sleep/resume | Device | Manual |
| Z01 | Native zoom label updates and reset completion | Native | Pass |
| Z02 | Repeated zoom in/out, limits, fit presets and rejected layouts | Unit/native | Pass |
| Z03 | Last page of 1,000-page PDF; pinch then immediate scroll | Native | Pass: two native runs |
| Z04 | Last-page words remain visible and anchor stays near that page | Native | Pass: two native runs |
| Z05 | Sideways pan and zoom around centered/narrow pages | Native/physical | Partial: native offsets/centering pass; actual trackpad pan in Z07 |
| Z06 | Interrupt zoom with tab switch, close, rotation or navigation | Review/native | Pass: activation and in-gesture navigation regressions fixed; geometry commands guarded |
| Z07 | Real two-finger pinch, diagonal scrolling and quick reversals | Trackpad | Manual |
| Z08 | Zoom buttons, keyboard and Ctrl+wheel after a pinch | Native/physical | Partial: native zoom commands pass; physical Ctrl+wheel in Z07 |
| L01 | Vertical, Horizontal, Wrapped, Page times no/odd/even spreads | Unit/native | Pass: all 12 combinations |
| L02 | Layout switching preserves the current page and valid extents | Unit/native | Pass |
| L03 | Spread-click option, both directions, cover and final spread | Native | Pass |
| L04 | Links, gaps, disabled option, selection and Hand drags with clicks | Native/physical | Partial: handlers/guards pass; physical drag and click feel remain |
| N01 | Page number draft, invalid input, first/last boundaries | Unit/native | Pass |
| N02 | Internal/named links, null coordinates, rotated destinations | Unit/native | Pass |
| N03 | Safe external links and unsupported PDF actions | Unit/native/review | Pass: action policy and native routing; browser launch intercepted |
| N04 | Thumbnails, selection, stale replies, rotation and zoom | Unit/native | Pass |
| N05 | Outline hierarchy, malformed/deep/cyclic outlines, navigation | Unit/native | Pass |
| N06 | Find: Unicode, case, whole words, wrap, no match and cancellation | Unit/native | Pass |
| N07 | Selection/copy across pages, permissions and late results | Unit/native | Pass: copy callback intercepted; actual clipboard in N08 |
| N08 | Actual clipboard and physical keyboard delivery | Desktop | Manual |
| T01 | Duplicate opens, multiple tabs, close/reopen and active tab | Native | Pass: Tabs steps 0-13 |
| T02 | Reorder, move shortcuts and tab overflow | Native/physical | Partial: reorder/lifecycle/width checks pass; physical drag remains |
| T03 | Window isolation and closing during background work | Engine/native | Pass: independent Windows mode, close during pending work |
| T04 | Explorer drops, file picker and canceled dialogs | Desktop | Blocked: storage broker denies drop fixture access at Tabs step 14; desktop picker/drop remains |
| S01 | Defaults, corrupt settings, migration and range validation | Unit | Pass |
| S02 | Atomic saves, failed-write rollback and shared state | Unit/native | Pass |
| S03 | General, Shortcuts, About; repeated open/close and modality | Native | Pass: Settings and Windows modes |
| S04 | Shortcut capture, cancel, clear, conflicts and focus guards | Unit/native | Pass: native capture/guards; physical focus in S07 |
| S05 | Dark/Obsidian persistence, live changes and PDF isolation | Unit/native/visual | Pass: native persistence/live/startup and sampled captures |
| S06 | Narrow/short settings windows, labels and clipped controls | Native/visual | Pass: narrow/short bounds and sampled captures; long bindings use ellipsis |
| S07 | Keyboard-only use, focus visibility, Narrator and high contrast | Desktop | Manual |
| P01 | Presentation entry/exit, fit, controls and state restoration | Native | Pass |
| P02 | Presentation links, endpoints, tab changes and close | Native | Pass |
| P03 | Physical keys, clicks, wheel and touch presentation | Desktop | Manual |
| A01 | Attachment metadata, Unicode names, empty/damaged streams | Engine/native | Pass |
| A02 | Attachment save cancellation, replacement and closed source tab | Unit/native | Pass |
| A03 | Save a Copy preserves loaded bytes exactly | Unit/native | Pass |
| A04 | Source alias, same path, hard links, atomic failures and late picker | Unit/native | Partial: all available cases pass; Windows denies hard-link fixture creation |
| A05 | Properties: metadata, permissions, huge/missing values and close | Unit/native | Pass: native metadata/lifecycle and sampled captures |
| A06 | Real save/overwrite dialogs and writable/read-only destinations | Desktop | Manual |
| J01 | Reproduce and isolate intermittent print-job crash/hang | Debug/Release stress | Pass: both causes reproduced, fixed and stress-tested; Q01/Q03 |
| J02 | Print preview, original colors, annotations and selected pages | Unit/native | Pass |
| J03 | Printer DPI, ranges, rotation, paper sizes and layout limits | Unit/integration | Pass |
| J04 | Cancel before/during a tile; close while pending; failing target | Integration/stress | Pass |
| J05 | Repeat jobs and wait for complete asynchronous teardown | Integration/stress | Pass: 50 shuffled ASan iterations, 250 checks |
| J06 | Microsoft Print to PDF, reopen and verify output pixels/pages | Explicit virtual driver | Pass: real Microsoft PDF driver in Debug and Release |
| J07 | Actual print dialog, physical printer, copies and collation | Desktop/device | Manual |
| M01 | Playing/recent session choice and session removal | Unit/native | Pass: injected sessions |
| M02 | Stale metadata/art rejection; seek/progress/control state | Unit/native | Pass: injected metadata/artwork |
| M03 | Bright/dark/no-art pane, bounds and reduced motion | Native/visual | Pass: native bounds/motion and bright/dark/no-art captures |
| M04 | Real Spotify/Apple Music, both players, pause/seek/switch | Media broker/device | Manual |
| X01 | Help/version, invalid CLI, redirection and exit codes | Unit/package smoke | Pass: CLI suite; final package smoke recorded below |
| X02 | Diagnostics, log rotation, unavailable storage and debug flags | Unit/native | Pass |
| X03 | Coroutine lifetimes, event teardown and engine-thread ownership | Targeted review | Pass: targeted session/print and gesture lifecycle review; not a whole-app audit |
| X04 | Repeat native gesture/layout/tab/close cycles | Stress | Partial: repeated gestures/layouts, window/close checks pass; OS drop remains blocked |
| X05 | Memory/cache bounds and process/resource cleanup after stress | Stress | Partial: sampled working sets, cache assertions and clean exits; no long-duration leak proof |
| X06 | Sanitizer or debugger diagnostics, if installed and compatible | Diagnostics | Pass: 3 full ASan suites and 50 focused repetitions; limits below |
| X07 | Bounded malformed-input and layout boundary exploration | Unit/generated cases | Pass: 120 mixed layouts and 192 damaged PDFs, deterministic seed 19026 |
| X08 | Dependency versions, license files and signing inventory | Static/package | Pass: pinned inputs/notices inventoried; unsigned EXEs and distribution review remain |
| D01 | Packaging tests: corruption, unsafe paths, mixed snapshots | Packaging | Pass: all 8 tests |
| D02 | x64/ARM64/source manifests, SHA-256 and architecture checks | Packaging | Pass: all three preflight archives |
| D03 | Fresh extraction; CLI and PDF startup/close | Package smoke | Pass: x64 preflight extraction, path with spaces |
| D04 | Release workflow/script parsing and source/package exclusions | Static | Partial: 5 scripts and 9 workflow blocks parse; first hosted run pending |
| D05 | Clean Windows PC/VM and actual ARM64 execution | Separate machine | Manual |
| D06 | Different DPI monitors, touch/pen, accessibility settings | Device | Manual |
| D07 | Opt-in notification display and click activation | Desktop | Manual |

## Findings

| ID | Severity | Reproduction / evidence | State |
|---|---|---|---|
| Q01 | Crash | ASan caught a retained print renderer releasing session state through an already freed Engine. Empty state now releases without touching the old engine; PDFium documents still close on the engine thread. | Fixed; focused regression and 50 shuffled print runs pass |
| Q02 | Needs physical confirmation | The last delivered build fixes reported long-document zoom/rendering behavior in native probes; the same book still needs a real trackpad retest. | Manual |
| Q03 | Test runner crash | Repeated COM shutdown invalidated cached PrintPageRange factories between tests. The runner now keeps its apartment for the whole suite, clears factories on final shutdown and waits for print-job completion. | Fixed; 50 shuffled print runs pass |
| Q04 | Reader regression | Native Gestures failed when a reactivated tab retained a pan flag at native 1x. Activation now clears that stale flag so zoom commands can work. | Fixed; extended Gestures passed twice |
| Q05 | Wrong page | Native Gestures reproduced navigation to page 3 after requesting the final page during native zoom. Navigation now uses scaled offsets. Layout, rotation and presentation commands are ignored during the native transform; they work again after it settles. | Fixed; extended Gestures passed twice |
| Q06 | Environment limit | The full Tabs check reaches step 14, then StorageFile access for the drop fixture fails with Access is denied. Steps 0-13 pass. The independent Windows mode checks the later window/settings cases without starting a drop. | Drop check blocked; Windows check passed |

## Run record

Local logs are in `.tmp-gesture-fix/qa-20261019/` (excluded from packages).

- Baseline print repetition: failed on iteration 2 with an access violation.
- `asan-lifetime-before.err`: heap-use-after-free in `Engine::post` from
  the session-state deleter, reached by the new retained-renderer regression.
- `asan-print-uncaught.err`: the second independent failure points to the
  cached `PrintPageRange` factory on iteration 2.
- `asan-print-apartment-fixed-50`: 50 shuffled iterations, seed 19026 and
  successive seeds, all 250 checks passed in 43.88 seconds. Peak sampled
  working set: 659.5 MiB. No sanitizer memory error was reported.
- ASan uses embedded compiler symbols (`/Z7`); `/Zi` failed with C1902 on
  this host. Allocator-pair mismatch checking uses its Windows default of
  off because the prebuilt GoogleTest DLL crosses an uninstrumented
  allocator boundary. Use-after-free and bounds checks remain enabled.
- `package-tests.out`: all eight package tests passed.
- Four normal builds passed with zero warnings. Debug logs are
  `.tmp-print-preview/reader-parity-{x64,ARM64}-Debug.log`; Release logs are
  `.tmp-print-preview/lto-host64-{x64,ARM64}.log`. Release retains normal LTO
  with the 64-bit compiler host. ARM64 was compiled, not executed.
- The complete x64 Debug and Release suites each ran 454 tests: 453 passed
  and the existing hard-link alias check skipped with access denied.
  The virtual-printer test is disabled by default and was run separately.
- `debug-full-shuffled-5`: five complete passes, initial seed 7519;
  `release-full-shuffled-5`: five complete passes, initial seed 19026.
  Each repetition uses the next seed. Peak sampled working sets were
  234.0 MiB and 249.3 MiB respectively; both processes exited normally.
- `asan-full-shuffled-3`: three complete passes, initial seed 19026,
  453 passes and one skip per repetition. No sanitizer error; peak sampled
  working set 738.8 MiB. These checks instrument Sable's test-linked C++;
  prebuilt PDFium, GoogleTest and system DLLs are not fully instrumented.
- The new robustness tests cover 120 generated mixed-size documents across
  all 12 layouts and 192 damaged/truncated PDF buffers. Seed 19026 makes
  these bounded cases repeatable. This is not a sustained fuzzing campaign.
- Twenty-five native modes passed: Gestures, LongGestures, Link,
  Presentation, Rotation, Pan, Layout, Selection, Find, Keyboard, Edit,
  Filter, Settings, Properties, SaveCopy, AttachmentSave, Attachments,
  Outline, Finish, Print, Music, Scale, Error, Obsidian and Windows.
  Logs use `native-final-*`, with Windows in `native-repeat2-Windows`.
  Gestures and LongGestures passed again in `native-repeat2-*` after the
  independent Windows mode was added. The full Tabs result remains Q06.
- Native modes invoke handlers and real WinUI layout/transforms. They do
  not prove physical keyboard, pointer or trackpad event delivery. Settings,
  music and save operations use isolated stores or injected test callbacks.
- Sampled captures were inspected for General at narrow/short sizes,
  shortcut rows and capture, Obsidian About/PDF/layout, narrow properties,
  and music with bright, dark and missing artwork. Controls fit and text
  stayed readable. Narrow shortcut bindings intentionally use ellipsis.
- `debug-virtual-print` and `release-virtual-print` both passed the explicit
  Microsoft Print to PDF test. Generated output was reopened with PDFium
  and checked for page range/count, paper sizes, colors, annotations and
  rotation. No physical printer was used.
- Five repository PowerShell scripts and nine release-workflow run blocks
  parsed without errors. The official action READMEs were checked against
  the workflow versions. This is not a hosted workflow execution.
- Cache bounds and clean child-process exits passed. Working-set samples
  above are observations, not a leak detector or a long-duration guarantee.
- Fresh x64, ARM64 and source preflight archives passed manifest, content
  hash, source-snapshot, architecture and notice checks. The extracted x64
  app passed help/version and PDF startup/close from a fresh path containing
  spaces (`preflight-smoke.out`). These use the final Release binaries.
  Documentation was completed after preflight; delivery archives are rebuilt
  and verified from that completed source snapshot.

## Completion boundary

Do not mark a public release ready with unresolved crashes, data loss,
missing page content, incorrect output or a failing required build. Keep
skips and environment limitations visible. Manual checks are described in
[READER_CHECKS.md](READER_CHECKS.md). Git commits, tags, publishing and the
first hosted workflow run remain outside this local test pass.

The next manual pass should start with the same book and trackpad that
showed the zoom problem, then cover OS dialogs/clipboard/drop, real music
players and display changes. A fresh Windows PC and an ARM64 device are
separate release checks. Keep actual results and device details alongside
this record instead of turning an unavailable check into a pass.
