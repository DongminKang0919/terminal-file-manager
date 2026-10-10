#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>
static uint64_t tick;
uint64_t __wrap_core_monotonic_ms(void) { return tick; }
static void feed(const char *s) { for(size_t n=strlen(s);n;n--) assert(unget_wch((unsigned char)s[n-1])==OK); }
static void escape(void) { feed("\033"); assert(input_key(stdscr)==ERR); tick+=25; assert(input_key(stdscr)==27); }
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm-256color",out,in); assert(screen); resizeterm(24,100); input_init(); wtimeout(stdscr,0);
    while(input_key(stdscr)==KEY_RESIZE) {}
    TerminalReply r; input_terminal_begin(&r); wtimeout(stdscr,50);
    feed("\033[?62;4c\033[6;16;8t"); assert(input_key(stdscr)==ERR);
    assert(r.sixel&&r.width==8&&r.height==16);
    feed("\033[999~"); assert(input_key(stdscr)==ERR);
    feed("q"); assert(input_key(stdscr)=='q');
    input_terminal_begin_timed(&r,tick+100);
    feed("\033[?62;"); assert(input_key(stdscr)==ERR); tick+=101;
    feed("4c\033[6;16;8t"); assert(input_key(stdscr)==ERR);
    assert(!r.da_received&&!r.cells_received); input_terminal_end();
    feed("q"); assert(input_key(stdscr)=='q');
    input_terminal_begin(&r);
    feed("?62;4c"); assert(ungetch(0x9b)==OK); assert(input_key(stdscr)==ERR); assert(r.da_received&&r.sixel);
    feed("6;16;8t"); assert(ungetch(0x9b)==OK); assert(input_key(stdscr)==ERR); assert(r.width==8&&r.height==16);
    input_terminal_begin(&r);
    feed("\033[?62;"); assert(input_key(stdscr)==ERR); input_terminal_end();
    feed("4c\033[6;16;8t"); assert(input_key(stdscr)==ERR); assert(!r.da_received&&!r.cells_received);
    feed("\033[?62;qqF8;999999999999c"); assert(input_key(stdscr)==ERR);
    feed("\033[62;qqqF8c"); assert(input_key(stdscr)==ERR);
    feed("\033["); assert(input_key(stdscr)==ERR);
    escape();
    feed("?62;4c"); assert(input_key(stdscr)==ERR);
    feed("\033[6;"); assert(input_key(stdscr)==ERR);
    assert(ungetch(KEY_RESIZE)==OK); assert(input_key(stdscr)==KEY_RESIZE);
    feed("16;8t"); assert(input_key(stdscr)==ERR);
    escape(); /* Cancel leaves ESC prefix guard for split late replies. */
    feed("[?62;4c"); assert(input_key(stdscr)==ERR);
    escape();
    feed("\033[18~"); assert(input_key(stdscr)==KEY_F(7));
    feed("\033[?999~q"); assert(input_key(stdscr)=='q');
    feed("\033[6;999~q"); assert(input_key(stdscr)=='q');
    feed("\033[999zq"); assert(input_key(stdscr)=='q');
    feed("\033[999~q"); assert(input_key(stdscr)=='q');
    feed("\033[999~\033OP"); assert(input_key(stdscr)==KEY_F(1));
    feed("\033[999~\033[A"); assert(input_key(stdscr)==KEY_UP);
    feed("\033[999~text"); for(const char *p="text";*p;p++) assert(input_key(stdscr)==*p);
    /* Cancel a malformed report, discard its tail, then recover at a new frame. */
    feed("\033[?62;qq"); assert(input_key(stdscr)==ERR); escape();
    feed("qqF8c"); assert(input_key(stdscr)==ERR);
    feed("q"); assert(input_key(stdscr)=='q');
    feed("\033[6;bad"); assert(input_key(stdscr)==ERR); escape();
    feed("\033[18~"); assert(input_key(stdscr)==KEY_F(7));
    feed("\033[?62;broken\033OA"); assert(input_key(stdscr)==KEY_UP);
    feed("plain"); for(const char *p="plain";*p;p++) assert(input_key(stdscr)==*p);
    feed("\033OA"); assert(input_key(stdscr)==KEY_UP);
    feed("\033[1;3D"); assert(input_key(stdscr)==UI_BACK);
    feed("\033[1;3C"); assert(input_key(stdscr)==UI_FORWARD);
    feed("[]"); assert(input_key(stdscr)=='['); assert(input_key(stdscr)==']');
    feed("\033[D\033[C\033OD\033OC");
    assert(input_key(stdscr)==KEY_LEFT); assert(input_key(stdscr)==KEY_RIGHT);
    assert(input_key(stdscr)==KEY_LEFT); assert(input_key(stdscr)==KEY_RIGHT);
    feed("\033[1;"); assert(input_key(stdscr)==ERR);
    feed("3C"); assert(input_key(stdscr)==UI_FORWARD);
    feed("\033[1;5Cq"); int modified=input_key(stdscr);
    assert(modified!=KEY_RIGHT && modified!=UI_FORWARD && modified!=UI_BACK);
    if(modified!='q') assert(input_key(stdscr)=='q');
    feed("\033[<0;4;5M"); assert(input_key(stdscr)==KEY_MOUSE); MEVENT e; assert(getmouse(&e)==OK&&e.x==3&&e.y==4);
    wint_t scalars[]={L'ć',L'한',0x1f642,0x80,0x9b,0x10ffff};
    for(unsigned i=0;i<sizeof scalars/sizeof *scalars;i++) {
        assert(unget_wch(scalars[i])==OK); wint_t c; assert(input_wide(stdscr,&c)==OK&&c==scalars[i]);
    }
    feed("abc123"); for(const char *p="abc123";*p;p++) assert(input_key(stdscr)==*p);
    endwin(); delscreen(screen); fclose(in); fclose(out);
    puts("PASS: split/late/malformed terminal report isolation, Esc/resize, regular keys, Unicode and mouse preservation");
}
