#include "../src/core/terminal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    TerminalReply r={0};
    const char *ok[]={"\033[?62;1;2;4;6;9c","\033[6;16;8t"};
    for(unsigned i=0;i<2;i++) assert(terminal_report(ok[i],strlen(ok[i]),&r));
    assert(r.da_received&&r.sixel&&r.cells_received&&r.width==8&&r.height==16);
    const char *bad[]={"\033[?4c","\033[6;0;8t","\033[6;129;8t","\033[6;16;65t","\033[6;999999999999999;8t","\033[?62;4;c","\033[?62;q4c","\033[6;16;8tr","\033[6;16;8","\033[6;-1;8t"};
    for(unsigned i=1;i<sizeof bad/sizeof *bad;i++) {
        TerminalReply before=r; assert(!terminal_report(bad[i],strlen(bad[i]),&r));
        assert(!memcmp(&before,&r,sizeof r));
    }
    assert(terminal_report(bad[0],strlen(bad[0]),&r)&&!r.sixel); /* Terminal class is not a feature. */
    for(size_t n=0;n<strlen(ok[0]);n++) assert(!terminal_report(ok[0],n,&r));
    char giant[129]; memset(giant,'0',sizeof giant); assert(!terminal_report(giant,sizeof giant,&r));
    puts("PASS: bounded terminal DA/cell protocol, full-format validation and transactional replies (no curses)");
}
