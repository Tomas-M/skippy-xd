# Upstream issue review, 2026-11-01

Reviewed all 15 open issues and the three existing open pull requests against upstream master `a71d912849c43b3633eff90becf0e6ac25c22804`. Each original fix has a separate branch and upstream PR. Existing PRs are merged with their authorship and have no duplicate submission.

## Original fixes

| Issue | Result | Upstream PR |
| --- | --- | --- |
| Slax crash report | Validate the saved paging cancellation target; handle root focus before main-window creation. | [571](https://github.com/felixfung/skippy-xd/pull/571) |
| [68](https://github.com/felixfung/skippy-xd/issues/68) | Stable in-place merge sort, returned-head callers, comparison-bound/link/stability regression tests. This fixes sorting, not every quadratic layout loop. | [572](https://github.com/felixfung/skippy-xd/pull/572) |
| [566](https://github.com/felixfung/skippy-xd/issues/566) | Derive pager highlight state from the selected desktop when preview remapping loses FocusOut. | [573](https://github.com/felixfung/skippy-xd/pull/573) |
| [473](https://github.com/felixfung/skippy-xd/issues/473) | Track actual focus recency for the initial Alt-Tab candidate, independently of stacking order. | [574](https://github.com/felixfung/skippy-xd/pull/574) |
| Current-monitor layout | Run the half-open layout loop once for the selected monitor instead of zero times. | [575](https://github.com/felixfung/skippy-xd/pull/575) |
| [438](https://github.com/felixfung/skippy-xd/issues/438) | Switch unmapped source windows to cached previews and preserve UnmapNotify through event batching. | [576](https://github.com/felixfung/skippy-xd/pull/576) |
| [371](https://github.com/felixfung/skippy-xd/issues/371) | Prefer a valid published EWMH stacking property even when omitted from _NET_SUPPORTED. | [577](https://github.com/felixfung/skippy-xd/pull/577) |

## Existing upstream PRs integrated in this fork

- [567](https://github.com/felixfung/skippy-xd/pull/567): refresh pixmaps during create/map batches and fix includeFrame capture IDs. Addresses [433](https://github.com/felixfung/skippy-xd/issues/433) and [532](https://github.com/felixfung/skippy-xd/issues/532). Completed one remaining GTK frame-extents reference after the window-ID rename so the integrated branch compiles. Unmap events remain available to our snapshot handler.
- [569](https://github.com/felixfung/skippy-xd/pull/569): configurable label visibility/position and validation of every SKIP_TASKBAR/SKIP_PAGER state atom. The current configuration uses `label/show=all|focused|none` and `label/position=top|mid|bottom`.
- [570](https://github.com/felixfung/skippy-xd/pull/570): optional limit on space reserved for oversized dock windows, disabled by default.

## Open issues without an additional code fix

| Issue | Assessment |
| --- | --- |
| [84](https://github.com/felixfung/skippy-xd/issues/84) | Pseudo-transparent paging without a compositor captures the parent through an unmap/redraw/map sequence. Removing movie flicker requires a separately designed capture/buffering path; no unverified rendering workaround is included. |
| [52](https://github.com/felixfung/skippy-xd/issues/52) | Independent per-output workspace/tag sets do not fit the current global desktop model. Correct support needs WM-specific workspace information/adapters and a model change. |
| [523](https://github.com/felixfung/skippy-xd/issues/523) | Interoperability feature request for external overlays, without a defined stacking/input contract. |
| [458](https://github.com/felixfung/skippy-xd/issues/458) | Dragging windows into a workspace bar is a new UI feature the maintainer explicitly does not intend to add. |
| [309](https://github.com/felixfung/skippy-xd/issues/309) | Hint-letter navigation is a new input mode; the maintainer favors an external hints tool. |
| [119](https://github.com/felixfung/skippy-xd/issues/119) | Packaging/adoption coordination rather than a specific reproducible defect. Slax has its own verified package recipe. |
| [277](https://github.com/felixfung/skippy-xd/issues/277) | Contributor recruitment, no defect. |
| [222](https://github.com/felixfung/skippy-xd/issues/222) | Project promotion, no defect. |

## Validation

Debian 13 builds with Chipmunk enabled; Meson list tests include empty/singleton/sorted/reversed/duplicate/random inputs through 8,192 nodes, stable ties, both links, node identities and a subquadratic comparison bound. The list tests also pass ASan/UBSan with leak detection. Regression assertions stay enabled in release builds.

Isolated Xvfb sessions with Openbox, i3 and bspwm cover root-focus daemon startup, closing the original window during paging then Escape, repeated paging/expose, current-monitor layout, initial MRU selection, cached previews after unmapping, and fullscreen stacking. The combined fork passes both includeFrame=true and false startup/cancellation checks. These tests do not establish correctness of every compositor or multi-output workspace model; the remaining issues above stay open.
