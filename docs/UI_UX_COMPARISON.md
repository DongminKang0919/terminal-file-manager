# UI text comparison

Captured from actual tfile processes before (e139ac0) and after this change,
using the same temporary fixture and a PTY at 100x24. These are reconstructed
terminal cells, not graphical terminal screenshots. Colors, font appearance
and Sixel pixels are not represented. No filesystem operation was executed.

## Before

Main screen (100x24, one marked item):

```text
 [F1 Help] [F2 New] [F3 Search] [F5 Copy] [F6 Move] [F7 Options] [F8 Delete] [F9 Menu] [F10 Quit]
 [<] [>]  Location: /tmp/tfile-ux-demo
┌─* Files (3) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ a-한글.txt                                │
│  Name                              Kind       Size  ││ File | 520 B | Mode 0644                  │
│  .hidden                           File        7 B  ││ Modified: 2026-10-08 20:27                │
│>*a-한글.txt                        File      520 B  ││───────────────────────────────────────────│
│  b-other.txt                       File        6 B  ││                                           │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Row 1                                     │
└─────────────────────────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 3 | Hidden: on | Selected: 1
 Enter: Open  Tab: Preview  Backspace: Parent  F1: Help  F9: Menu
```

Options (same session):

```text
 [F1 Help] [F2 New] [F3 Search] [F5 Copy] [F6 Move] [F7 Options] [F8 Delete] [F9 Menu] [F10 Quit]
 [<] [>]  Location: /tmp/tfile-ux-demo
┌─* Files (3) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ a-한글.txt                                │
│  Name                              Kind       Size  ││ File | 520 B | Mode 0644                  │
│  .hidden                           File        7 B  ││ Modified: 2026-10-08 20:27                │
│>*a-한글.txt                        File      520 B  ││───────────────────────────────────────────│
│  b-other.txt          ┌ Options                                     [ x ]┐                       │
│                       │ [x] Show hidden files                            │                       │
│                       │ [x] Show preview panel                           │                       │
│                       │ Wheel scroll: 1 rows (click to change)           │                       │
│                       │ Sort by: Name (click to change)                  │                       │
│                       │ Sort order: Ascending (click to change)          │                       │
│                       │ Image preview: Auto (click to change)            │                       │
│                       │─Active panel sort/hidden saved as defaults───────│                       │
│                       │ Up/Down  Enter: choose  Esc: close               │                       │
│                       └──────────────────────────────────────────────────┘                       │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Row 1                                     │
└─────────────────────────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 3 | Hidden: on | Selected: 1

```

## After

Main screen (100x24, one marked item):

```text
 [F1 Help] [F2 New] [F3 Search] [F5 Copy] [F6 Move] [F7 Options] [F8 Delete] [F9 Menu] [F10 Quit]
 [<] [>]  Location: /tmp/tfile-ux-demo
┌─* Files (3) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ a-한글.txt                                │
│  Name                              Kind       Size  ││ File | 520 B | Mode 0644                  │
│  .hidden                           File        7 B  ││ Modified: 2026-10-08 20:27                │
│>*a-한글.txt                        File      520 B  ││───────────────────────────────────────────│
│  b-other.txt                       File        6 B  ││                                           │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Row 1                                     │
└─────────────────────────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 3 | Marked: 1 | Dotfiles: on
 Enter: Open  Space: Mark  Tab: Preview  Backspace: Parent  F1: Help  F9: Menu
```

Options (same session):

```text
 [F1 Help] [F2 New] [F3 Search] [F5 Copy] [F6 Move] [F7 Options] [F8 Delete] [F9 Menu] [F10 Quit]
 [<] [>]  Location: /tmp/tfile-ux-demo
┌─* Files (3) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ a-한글.txt                                │
│  Name                              Kind       Size  ││ File | 520 B | Mode 0644                  │
│  .hidden                           File        7 B  ││ Modified: 2026-10-08 20:27                │
│>*a-한글.txt                        File      520 B  ││───────────────────────────────────────────│
│  b-other.txt          ┌ Options                                     [ x ]┐                       │
│                       │ [x] Show hidden files                            │                       │
│                       │ [x] Show preview panel                           │                       │
│                       │ Wheel scroll: 1 rows (click to change)           │                       │
│                       │ Sort by: Name (click to change)                  │                       │
│                       │ Sort order: Ascending (click to change)          │                       │
│                       │ Image preview: Auto (click to change)            │                       │
│                       │─Active panel sort/hidden saved as defaults───────│                       │
│                       │ Tab/Arrows  Enter: choose  Esc: close            │                       │
│                       └──────────────────────────────────────────────────┘                       │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Preview line                              │
│                                                     ││ Row 1                                     │
└─────────────────────────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 3 | Marked: 1 | Dotfiles: on

```
