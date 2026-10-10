# UI 역할 표시 변경 전후 — 2026-10-10

동일한 임시 경로·파일·수정 시간·키 입력과 화면 크기로 실제 실행 프로세스에서 캡처한 PTY 셀입니다. 변경 전은 작업 시작 시 보관한 실행 파일, 변경 후는 수정한 소스의 실행 파일입니다. 그래픽 터미널 스크린샷이 아니며 색상·글꼴·Sixel 픽셀·깜빡임은 이 기록으로 판단할 수 없습니다. `a-한글.txt`를 마킹한 뒤 커서를 `b-other.txt`로 이동했습니다.

## 변경 전: 100×24

```text
 [F1 Help] [F2 New] [F3 Search] [F5 Copy] [F6 Move] [F7 Options] [F8 Delete] [F9 Menu] [F10 Quit]   
 [<] [>]  Location: /tmp/tfile-ui-comparison-fixture                                                
┌─* Files (4) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ b-other.txt                               │
│  Name                              Kind       Size  ││ File | 17 B | Mode 0644                   │
│  a-dir                             Dir           -  ││ Modified: 2026-10-10 08:00                │
│  .hidden                           File        7 B  ││───────────────────────────────────────────│
│ *a-한글.txt                        File      520 B  ││                                           │
│> b-other.txt                       File       17 B  ││ Cursor file body                          │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
└─────────────────────────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 4 | Marked: 1 | Dotfiles: on  Targets: 1 marked; cursor ignored. a: all / u: clear          
 Enter: Open  Space: Mark  e: Vim  F1: Help  F9: Menu  a: All  u: Clear                             
```

## 변경 후: 100×24

```text
 [F1 Help] [F3 Search]  [F2 New] [F5 Copy] [F6 Move] [F8 Delete]  [F7 Options] [F9 Menu] [F10 Quit] 
 [<] [>]  Location: /tmp/tfile-ui-comparison-fixture                                                
┌─* Files (4) | Name ascending ───────────────────────┐┌─  Preview ────────────────────────────────┐
│ [Parent]  [Open]                                    ││ b-other.txt                               │
│  Name                              Kind       Size  ││ File | 17 B | Mode 0644                   │
│  a-dir                             Dir           -  ││ Modified: 2026-10-10 08:00                │
│  .hidden                           File        7 B  ││───────────────────────────────────────────│
│ *a-한글.txt                        File      520 B  ││                                           │
│> b-other.txt                       File       17 B  ││ Cursor file body                          │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
│                                                     ││                                           │
└─ Targets: 1 marked ─────────────────────────────────┘└───────────────────────────────────────────┘
 Shown: 4 | Marked: 1 | Dotfiles: on  Targets: 1 marked                                             
 Enter: Open  Space: Mark  e: Vim cursor  F1: Help  F9: Menu  a: All  u: Clear                      
```

## 변경 전: 50×9

```text
 [F1] [F2] [F3] [F5] [F6] [F7] [F8] [F9] [F10]    
 [<] [>]  Location: ... ile-ui-comparison-fixture 
┌─* Files (4) Name+ ─────────┐┌─  Preview ───────┐
│ [Parent]  [Open]           ││b-other.txt       │
│ *a-한글.txt          File  ││──────────────────│
│> b-other.txt         File  ││Cursor file body  │
└─ Shown 3-4/4 ──────────────┘└──────────────────┘
 Shown: 4 Marked: 1  Targets: 1 marked; cursor... 
 Enter: Open  Space: Mark  e: Vim  F1: Help       
```

## 변경 후: 50×9

```text
 [F1 Help] [F3 Search]  [F9 Menu] [F10 Quit]      
 [<] [>]  Location: ... ile-ui-comparison-fixture 
┌─* Files (4) Name+ ─────────┐┌─  Preview ───────┐
│ [Parent]  [Open]           ││b-other.txt       │
│ *a-한글.txt          File  ││──────────────────│
│> b-other.txt         File  ││Cursor file body  │
└─ Targets: 1 marked 3-4/4 ──┘└──────────────────┘
 Shown: 4 Marked: 1  Targets: 1 marked            
 Enter: Open  Space: Mark  e: Vim cursor          
```
