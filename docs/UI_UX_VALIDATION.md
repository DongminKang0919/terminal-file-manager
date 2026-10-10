# UI/UX consistency — 2026-10-08

최신 역할 표시 변경과 검사는 [2026-10-10 검증 기록](UI_ROLE_VALIDATION_2026-10-10.md), [동일 fixture의 실제 PTY 비교](UI_ROLE_COMPARISON_2026-10-10.md)를 참고하세요. 아래는 이전 변경의 기록입니다.

## Audit and rules

The current README and UI code already shared popup frames, safe UTF-8 display,
cursor/mark columns, Cancel-first deletion, retained result details, independent
dual panels and bounded centered media previews. These behaviors were retained.
No core or platform filesystem operation was changed.

The remaining inconsistencies were ambiguous Sel/Selected/S/H/M summaries,
setting/count ambiguity around Hidden, warning colors for ordinary Options
information, separately calculated button hit regions, missing Shift+Tab in
single deletion, and missing button focus in search results and pickers.

- Shown counts loaded entries. Marked counts operation targets; it is independent
  of the cursor. Dotfiles: on/off is the display setting. Narrow layouts drop the
  setting first, then the redundant active-side name and Shown summary if needed
  to preserve primary alerts; Marked remains visible. Routine guidance uses its
  explanatory text without a redundant severity prefix.
- Main commands, current path, panel titles, property headings, count summary,
  retained alerts and focus-sensitive shortcuts retain separate visual roles.
  Semantic alert styles distinguish warning/error from ordinary information.
  Partial operations say Partial; successful operations with failed refreshes
  retain Success and show refresh failed as a separate issue.
- Common button hit detection uses safe cell widths, clipping and ellipsis,
  and rejects disabled controls. Tab/Shift+Tab skips disabled controls. Menu and
  Options choices, search results, picker controls and deletion now share the
  expected navigation. List navigation and progress cancellation remain exceptions.
- Destination validation preserves edits and returns focus to that input. Source
  is labeled explicitly and batch destination titles identify target counts.
  Permanent deletion retains Cancel by default and the all-contents warning.
- A common path display helper preserves the last directory/file name. Existing
  Paths paging, picker path input and result-detail scrolling still expose full paths.
- Media Fit, bounds, resize coalescing, detection and preview capability policy
  remain unchanged. Popup restoration consumes cached data without extra I/O.

## Verification

Behavioral checks cover cursor versus marked targets, disabled mouse/Enter
controls, Tab/Shift+Tab, search result opening, permanent delete confirmation,
acknowledgement, clipped wide button labels and Unicode directory tails.
Existing permission, collision, cancellation, partial-result, refresh-failure,
resize, dual-panel, modal restoration, media and chained workflow checks use only
temporary fixture directories.

Final `make -j4 check` completed with exit code 0. It covers core/platform,
settings, terminal detection/input, media Fit and lifecycle, native UI and the
complete PTY suite, including chained workflow, recovery, batch destination,
batch execution and partial cancellation. During development, old assertions
for fixed menu indexes, unconditional Enter: Open on empty lists and the active
side's old footer position were updated to the intentional behavior. Narrow
search status tests also exposed excessive prefix/summary space; the final
layout now preserves the primary cancellation/incomplete-search message.

`git diff --check`, dependency architecture and Python syntax checks passed.
Local logical commits separate shared summaries/styles/control geometry from
specialized modal navigation, regression coverage and documentation. No remote
push was performed.

Before/after main and Options text was captured from actual application processes
at 100x24, against the same fixture. See [the recorded cells](UI_UX_COMPARISON.md).
These are PTY cell reconstructions, not graphical screenshots. Colors, font
contrast, real Sixel pixels, clearing artifacts and flicker were not visually
verified in a graphical terminal. Logical palettes are checked in ncurses at
256 colors, 8 colors and monochrome.

ASan/UBSan native checks used:

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-ux-release-20261008 --disable-leaks
```

All 17 targets passed. Leak detection was disabled because earlier runs established
that this execution environment rejects LeakSanitizer under ptrace. It was not
repeated under that condition. ASan/UBSan success does not establish leak freedom.
For a fresh manual check outside ptrace, with leak detection enabled:

```sh
python3 tests/isolated_check.py python3 tests/run_sanitizers.py \
  --output-directory /tmp/tfile-sanitizers-ux-manual-20261008
```

This native script does not run every PTY under sanitizers or visually inspect a
terminal. Existing owned-memory/FD/window lifetime tests remain a separate check.

## Remaining limitations

Very narrow screens cannot display all supplemental fields or shortcuts at once.
Full names/paths and long diagnostics use existing paging/details mechanisms.
A resize still closes an active form instead of preserving its window in place;
valid main focus and selection are restored, and image geometry is recalculated.
The existing Sixel-only graphics support, full repaint flicker, first-page PDF,
conversion limits and lack of automatic rollback remain unchanged.
