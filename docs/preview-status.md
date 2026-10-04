# Preview status layout

File name, kind/size/mode and modification date stay above a separator while
text or PDF content scrolls below it. Images use the same reserved header and
content bounds. On short panels metadata contracts to the file name, spacing
shrinks and the row footer is omitted to preserve room for the status title.

Status blocks are left aligned with cell-aware UTF-8 wrapping and extra side
padding on wider panels. At normal heights their title begins roughly one third
of the way down the content area. Titles are bold; reasons and optional recovery
instructions use the normal text color. ASCII markers work without colors.

| State | Title | Reason / recovery |
| --- | --- | --- |
| Binary or unsupported file type | `[i] Preview unavailable` | Explains the unsupported content; no retry suggestion |
| Zero-byte file | `[i] Empty file` | File contains no data |
| Verified empty directory | `[i] Empty directory` | Directory contains no entries |
| Image Auto off | `[i] Preview disabled` | Existing disabled hint; F7 configuration guidance |
| Capability query or conversion pending | `[...] Loading preview` | Existing preparation hint |
| Read, metadata or conversion error | `[!] Preview failed` | Actual error detail; refresh guidance |
| Missing converter / image configuration | `[i] Preview unavailable` | Existing precise detail; warning color for missing tools/settings |
| PDF page without extractable text | `[i] Preview unavailable` | No text could be extracted from page 1; never called an empty file |

Neutral information uses the existing cyan information color; setup warnings
use yellow and real failures use the existing red error color. Hidden-only
directories continue to be nonempty; directory read errors continue to be
failures. The existing core/platform state and conversion policies are retained.
Only viewport geometry changes to accommodate the separated header.

## Verification

- Directly inspected the running application's reconstructed PTY cells at
  100x24 for a long Korean file name and binary status: clipped name, metadata,
  separator, spacing and left-aligned reason.
- Automated ncurses cell tests cover empty files/directories, hidden-only
  directories, permission/read failures, bold titles, long Korean errors,
  loading/disabled states, narrow panel boundaries, cache/scrolling and
  metadata retention; monochrome terminal behavior is also exercised.
- Automated layout PTY checks cover 50x9, 80x24, 100x24 and 160x32,
  normal text/EOF, binary, link and read-error previews.
- Media tools and PTY regressions cover image/PDF success, text fallback,
  PDF pages without text, converter errors/missing tools, selection changes,
  resizing, cancellation, graphics clearing and output bounds.
- Terminal regressions cover detection/setup, resize, Unicode input and
  missing-tool/geometry behavior. Architecture and whitespace checks pass.

No graphical terminal was available for a manual color or real Sixel pixel
inspection. PTY graphics checks validate emitted bytes, placement, clearing and
lifecycle, not how a real terminal paints pixels. No manual pixel verification
is claimed.
