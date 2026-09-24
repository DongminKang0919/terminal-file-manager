#include "ui.h"
#include <limits.h>

/* ncurses exposes only five mouse buttons. Decode SGR reports here so side
   buttons remain distinct from wheel events, including in text-entry dialogs.
   Protocol: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html */
void input_init(void) {
    mouseinterval(0);
    mousemask(BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED |
              BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    define_key("\033[<", UI_SGR_MOUSE);
    define_key("\033[1;3D", UI_BACK);
    define_key("\033[1;3C", UI_FORWARD);
}
static int decode_mouse(WINDOW *win) {
    unsigned values[3] = {0};
    int field = 0, digits = 0, result = 0;
    int delay = wgetdelay(win);
    wtimeout(win, 25);
    for (int n = 0; n < 48; n++) {
        int ch = wgetch(win);
        if (ch >= '0' && ch <= '9') {
            if (values[field] > (unsigned)(INT_MAX - 9) / 10) break;
            values[field] = values[field] * 10 + (unsigned)(ch - '0'); digits++;
        } else if (ch == ';' && digits && field < 2) { field++; digits = 0; }
        else if ((ch == 'M' || ch == 'm') && field == 2 && digits) {
            if (!values[1] || !values[2]) break;
            unsigned button = values[0] & ~(4u | 8u | 16u);
            if (button == 128 || button == 129) {
                if (ch == 'M') result = button == 128 ? UI_BACK : UI_FORWARD;
                break;
            }
            MEVENT event = { .x = (int)values[1] - 1, .y = (int)values[2] - 1 };
            if (button == 0) event.bstate = ch == 'M' ? BUTTON1_PRESSED : BUTTON1_RELEASED;
            else if (button == 64 && ch == 'M') event.bstate = BUTTON4_PRESSED;
            else if (button == 65 && ch == 'M') event.bstate = BUTTON5_PRESSED;
            else break; /* Ignore motion, horizontal wheel, and other buttons. */
            if (values[0] & 4) event.bstate |= BUTTON_SHIFT;
            if (values[0] & 8) event.bstate |= BUTTON_ALT;
            if (values[0] & 16) event.bstate |= BUTTON_CTRL;
            if (ungetmouse(&event) == OK) result = wgetch(win); /* consume its queued KEY_MOUSE */
            break;
        } else { if (ch == KEY_RESIZE) result = KEY_RESIZE; break; }
    }
    wtimeout(win, delay);
    return result;
}
int input_key(WINDOW *win) {
    int key = wgetch(win);
    return key == UI_SGR_MOUSE ? decode_mouse(win) : key;
}
int input_wide(WINDOW *win, wint_t *key) {
    int kind = wget_wch(win, key);
    if (kind == KEY_CODE_YES && *key == UI_SGR_MOUSE) {
        *key = (wint_t)decode_mouse(win);
        return KEY_CODE_YES;
    }
    return kind;
}
