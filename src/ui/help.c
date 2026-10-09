#include "ui.h"

typedef struct { const char *key, *text; } HelpEntry;
/* A NULL key is a section heading; an empty key is an explanatory paragraph. */
static const HelpEntry entries[] = {
    {NULL, "GETTING STARTED"},
    {"", "Select a file to preview it. Open a directory to explore it."},
    {"F9 / m", "Open the full menu; commands also work with the mouse."},
    {NULL, "NAVIGATION"},
    {"Up/Down, k/j", "In Files, select an item; click also selects."},
    {"PgUp/PgDn", "Move one list page. Home/End selects first/last."},
    {"Enter / Right", "In Files, open a directory or show the file preview (focus it when scrollable); double-click or Open also works."},
    {"Backspace / Left", "In Files, go to the parent directory; Parent also works."},
    {"Alt+Left/Right", "Back/forward restores the visited selection and list scroll. [ / ], Location buttons and mouse side buttons also work if supported."},
    {"Tab / Shift+Tab", "Switch Files/Preview when preview can scroll. Click a scrollable preview to focus it; * marks the active title."},
    {"r", "Refresh the current directory and preview."},
    {"q / F10", "Quit from the main screen."},
    {NULL, "SCREEN MODES AND FILE PANELS"},
    {"F9 > View", "Files + Preview / Files only / Left + Right files. Existing F keys are unchanged. The active list is retained when leaving dual mode."},
    {"Tab / Shift+Tab", "In dual mode switch Left/Right without reloading. Click a visible panel to activate it. * title, highlighted border/row and > cursor are active; : is the inactive cursor."},
    {"", "Each file panel owns path, sort, hidden setting, cursor, scroll, marks and history. All navigation, find/search and file actions use only the active panel. Wheel step and recent result are shared."},
    {"Narrow screens", "Below 80 columns dual mode shows only the active list at full width. Left/Right and Tab: Other identify the second panel. Resizing retains both panels and all marks."},
    {"Single mode", "Explicitly hiding a panel clears its marks, but retains path/cursor/scroll/history. Showing it again refreshes once and reselects by original name. STALE lists block file changes until r or successful navigation loads a fresh list."},
    {"Enter on a file", "Dual mode becomes Files + Preview from the active list and focuses Preview when scrollable; Esc returns to that list. Directory Enter opens only that panel."},
    {"File operations", "Only active marks are targets. Dual Copy/Move defaults to the opposite directory, even when narrow screens hide it. Relative/blank input uses the frozen source Base. Editing/Browse never navigates the peer. Destination errors retain input; opposite STALE is advisory and current paths are revalidated. After execution the other dual panel refreshes once. Details retain both refresh results separately from the file outcome."},
    {NULL, "MULTIPLE SELECTION"},
    {"Space in Files", "Toggle * on the cursor item without moving it. > is the cursor; * marks operation targets. Space in Preview or input does not change marks."},
    {"F9 menu", "Select all visible items or clear selection. Shown counts loaded items; Marked counts operation targets. Dotfiles: on/off is the hidden-file display setting. > is the cursor; * marks a target. Narrow screens omit the setting before either count."},
    {"", "Sort and refresh retain existing marks. Hidden-off drops hidden marks. Changing directory clears marks; history never saves them. Ctrl+F moves only the cursor."},
    {NULL, "FILE OPERATIONS"},
    {"F2 / F4", "Create a file or directory / start with Directory selected."},
    {"Tab / Shift+Tab", "Change form focus. Left/Right or Space changes the selected file/directory type."},
    {"Input errors", "Empty or oversized input shows a warning and keeps the value and edit cursor. Correct it and retry."},
    {"F9 > Rename", "Rename the selected item directly. Unchanged names do no work; collisions keep input open. The renamed item stays selected."},
    {"F5 / F6 / F8", "Marked items are targets; without marks use the cursor item. Multiple targets use one destination and original names; Rename is disabled with multiple marks."},
    {"F5 / F6", "For one target, choose destination/name then Copy now / Move now. For several, enter a destination, inspect the wrapped source list and destination, Tab from Cancel to Execute and Enter."},
    {"Batch destination", "Type or Browse using the single-transfer picker. Base is fixed for relative paths. Errors retain input/cursor. Enter/Next validates and reviews; confirmation Esc/Cancel returns to editing. Form Esc/Cancel or resize closes without changes or replacing recent results."},
    {"Batch confirmation", "Arrows/wheel, PgUp/PgDn, Home/End scroll targets without selecting Delete. Cancel is the default. Tab changes button focus; mouse buttons also work."},
    {"Batch results", "Run in current list order; stop at first error or cancellation. Details show Success/Failed/Cancelled/Unexecuted, partial changes, recursive counts and bytes. Main list refreshes once."},
    {"", "Successes lose marks; failed/cancelled/unexecuted targets retain marks if still visible. No automatic retry, rollback or leftover deletion. Move checks cancellation between targets only."},
    {"Picker", "Arrows browse; Enter opens. Space chooses a source or the current destination directory; p enters a path."},
    {"", "Existing destinations are never overwritten. Duplicate names keep the form open. Moves require the same filesystem."},
    {"F8 / Delete", "Confirm deletion; Cancel is selected by default. PgUp/PgDn or page buttons reveal long names."},
    {"", "Deletion includes directory children, bypasses the trash and cannot be undone."},
    {"Copy/Delete", "Cancel, Esc, Enter, x or resize requests a stop. Completed changes and incomplete copies are kept; deleted items are not restored."},
    {"", "Cancellation waits for the current filesystem call. Move is a single call with no in-flight cancellation. Refresh failures are reported separately from operation results."},
    {NULL, "OPERATION NOTIFICATIONS"},
    {"! / F9 menu", "Open the latest operation details; clicking the status row also opens it. Shown/Hidden remain visible alongside result labels."},
    {"z", "Dismiss the notification display. Details remain until the next completed file operation replaces them; no timed expiry."},
    {"Details", "Arrows/wheel, PgUp/PgDn, Home/End scroll. a/Enter or Ack acknowledges; Esc/x closes without acknowledgement."},
    {"", "Operation and list refresh results are separate. Counts are completed items and written bytes, not total progress. Diagnostics retain Result field limits."},
    {NULL, "SEARCH"},
    {"Ctrl+F", "Find in the loaded list (also F9 menu). Type a substring; Up/Down cycles matches. Enter selects without opening; Esc/resize restores selection and list scroll. Letters are input, not commands."},
    {"F3 or /", "Search file names below the current directory, including hidden entries; not file contents."},
    {"Input", "Enter or Search starts. Tab/Shift+Tab changes focus. Cancel/Esc/x closes. Results expand in the same window."},
    {"While scanning", "Cancel/Esc/Enter keeps collected results. x or resize stops and closes. Errors, skipped entries and limits are reported."},
    {"Results", "Tab/Shift+Tab switches results, Search and enabled Open. Arrows, wheel or PgUp/PgDn select. Enter/Open or double-click opens; relative paths distinguish matching names."},
    {"F3 or /", "Search again with the previous query ready to edit. Esc/x closes results. Resize closes the search window."},
    {"b / F9 Favorites", "Add current directory (a), rename (r), unregister (d), Enter to visit in the active panel. Changes persist separately from startup settings; unregister never deletes directories."},
    {NULL, "PREVIEW"},
    {"F7 > Show preview", "Hide the preview panel for a full-width file list. Selection and list scroll stay in place."},
    {"Up/Down, k/j", "With Preview focused, scroll one row. PgUp/PgDn scroll a visible page; Home returns to the start."},
    {"Esc / Tab", "Return to Files without moving selection or list scroll. F-key operations still target the marked file."},
    {"", "Opening the same prepared file preserves its preview position and cache. No external viewer or executable is launched."},
    {"Wheel on preview", "Scroll metadata and text. Row is the first visible display row; End means no content below."},
    {"", "Changing files resets scroll. Sorting preserves the selected file and preview. Refresh retries a read error. Directory and link details are shown without reading their contents."},
    {"", "Binary files have no text preview. Long lines are split, tabs become spaces, and unsafe characters are escaped. No file editing is provided."},
    {NULL, "OPTIONS"},
    {"F7", "Change hidden-file display, preview visibility, wheel step (1/3/5), sort and image Auto/Off. Save startup defaults explicitly in Options; sort/hidden defaults come from the active panel. Auto detects Sixel and cell pixels once per session when media needs it. Image display setup is a manual recovery test of display, erasure and session activation."},
    {"Enter / click", "Apply the option immediately and keep its focus and scroll. Esc/Done closes without undoing changes. Read failures retain the old hidden setting and list."},
    {"Sorting", "Name, size, modified time or kind; ascending/descending. Compact + means ascending, - descending."},
    {"Wheel on list", "Move selection by the configured number of rows."},
    {NULL, "POPUPS AND HELP"},
    {"Esc / x", "Close a popup, except an active search scan where Esc keeps results. Resize closes dialogs and cancels active scans/jobs."},
    {"", "Popup input cannot operate the background. Closing restores the panel focus, main selection and preview position. A hidden preview returns focus to Files."},
    {"Help controls", "Wheel/Arrows scroll one display line; PgUp/PgDn scroll a page; Home/End goes to the start/end. Esc, x, F1, q or F10 closes Help."},
};

typedef struct { char text[96]; bool heading; } HelpRow;
static bool help_rows(HelpRow *rows, size_t capacity, int width, size_t *total) {
    *total = 0;
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; i++) {
        const HelpEntry *e = &entries[i];
        if (!e->key) {
            if (*total + 2 > capacity) return false;
            if (*total) rows[(*total)++] = (HelpRow){0};
            HelpRow *row = &rows[(*total)++]; row->heading = true;
            snprintf(row->text, sizeof row->text, "%s", e->text);
            continue;
        }
        /* Wide screens use aligned columns; narrow screens give descriptions
           their own indented lines rather than cutting off the right column. */
        bool columns = width >= 60 && *e->key;
        if (!columns && *e->key) {
            if (*total == capacity) return false;
            snprintf(rows[*total].text, sizeof rows[*total].text, "%s", e->key);
            rows[(*total)++].heading = false;
        }
        int indent = columns ? 20 : *e->key ? 2 : 0;
        const char *text = e->text; bool first = true;
        while (*text) {
            if (*total == capacity) return false;
            size_t n = ui_text_span(text, 0, width - indent).end;
            if (text[n]) {
                size_t word = n;
                while (word && text[word] != ' ') word--;
                if (word) n = word;
            }
            HelpRow *row = &rows[(*total)++]; row->heading = false;
            snprintf(row->text, sizeof row->text, "%*s%.*s", indent, columns && first ? e->key : "", (int)n, text);
            /* Left align the key within the shared shortcut column. */
            if (columns && first) {
                memset(row->text, ' ', (size_t)indent);
                memcpy(row->text, e->key, strlen(e->key));
            }
            text += n; while (*text == ' ') text++; first = false;
        }
    }
    return true;
}

void show_help(UiContext *ui) {
    int screen_h, screen_w; getmaxyx(stdscr, screen_h, screen_w);
    if (screen_h < 9 || screen_w < 50) { message(ui, "Terminal too small for help"); return; }
    int h = screen_h - 2, w = screen_w - 8;
    if (w > 78) w = 78;
    WINDOW *win = dialog_open(ui, "Help", h, w);
    if (!win) { message(ui, "Cannot open help window"); return; }
    HelpRow lines[384]; size_t total;
    if (!help_rows(lines, sizeof lines / sizeof lines[0], w - 4, &total)) {
        dialog_close(ui, win); message(ui, "Help text exceeds display capacity"); return;
    }
    size_t offset = 0, rows = (size_t)(h - 4);
    size_t max = total > rows ? total - rows : 0;
    for (;;) {
        dialog_frame(win, "Help");
        size_t end = offset + rows < total ? offset + rows : total;
        for (size_t i = offset; i < end; i++) {
            if (lines[i].heading) wattron(win, A_BOLD);
            draw_window_text(win, 1 + (int)(i - offset), 2, w - 4, lines[i].text);
            wattroff(win, A_BOLD);
        }
        char hint[96];
        snprintf(hint, sizeof hint, "Lines %zu-%zu/%zu | %s %s", offset + 1, end, total,
                 offset ? "^ more" : "Top", end < total ? "v more" : "End");
        draw_window_text(win, h - 3, 2, w - 4, hint);
        draw_window_text(win, h - 2, 2, w - 4, "Wheel/Arrows/PgUp/PgDn  Esc: close");
        dialog_refresh(win);
        int key = mouse_key(win, input_key(win));
        if (key == KEY_RESIZE || key == 27 || key == KEY_F(1) || key == 'q' || key == KEY_F(10)) break;
        if (key == KEY_UP && offset) offset--;
        if (key == KEY_DOWN && offset < max) offset++;
        if (key == KEY_PPAGE) offset = offset > rows ? offset - rows : 0;
        if (key == KEY_NPAGE) offset = offset + rows < max ? offset + rows : max;
        if (key == KEY_HOME) offset = 0;
        if (key == KEY_END) offset = max;
    }
    dialog_close(ui, win);
}
