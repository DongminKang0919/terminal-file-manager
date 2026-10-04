#include "ui.h"

/* After an explicit query, read escape frames before ncurses' key decoder.
   Fragments survive timeouts/modal closure: a late report body never becomes
   shortcut keys. Ordinary Unicode and decoded keys are delivered individually.
   No input flush or tty mode changes. */
static bool armed, esc_delivered, overflow, report_escape;
static char frame[129];
static size_t used;
static TerminalReply *capture;
static uint64_t escape_started;
static wint_t queued;
static bool has_queued;
static unsigned utf_remaining;
static uint32_t utf_value,utf_minimum;
static int byte_queued=-1;
/* Keep byte-level C1 CSI distinct from a UTF-8 encoded U+009B. Reading one
   byte per iteration also bounds fragmented Unicode/protocol input latency. */
static int raw_wide(WINDOW *win,wint_t *out) {
    int c=byte_queued>=0 ? byte_queued : wgetch(win); byte_queued=-1;
    if(c==ERR) return ERR;
    if(c>255) { *out=(wint_t)c; return KEY_CODE_YES; }
    if(!utf_remaining) {
        if(c==0x9b) return 3; /* 8-bit CSI, normalized to ESC [ below. */
        if(c<128) { *out=(wint_t)c; return OK; }
        if(c>=0xc2&&c<=0xdf) { utf_remaining=1;utf_value=(unsigned)c&31u;utf_minimum=128; }
        else if(c>=0xe0&&c<=0xef) { utf_remaining=2;utf_value=(unsigned)c&15u;utf_minimum=2048; }
        else if(c>=0xf0&&c<=0xf4) { utf_remaining=3;utf_value=(unsigned)c&7u;utf_minimum=65536; }
        return 4; /* One byte consumed; keep reading within the call budget. */
    }
    if(c<0x80||c>0xbf) { utf_remaining=0;byte_queued=c;return 4; }
    utf_value=(utf_value<<6)|((unsigned)c&63u);
    if(--utf_remaining) return 4;
    if(utf_value<utf_minimum||utf_value>0x10ffff||(utf_value>=0xd800&&utf_value<=0xdfff)) return 4;
    *out=(wint_t)utf_value;return OK;
}
void terminal_input_reset(void) { armed=false; capture=NULL; used=0; has_queued=false; overflow=false; esc_delivered=false; utf_remaining=0; byte_queued=-1; report_escape=false; }
bool terminal_input_armed(void) { return armed; }
void input_terminal_begin(TerminalReply *reply) { armed=true; capture=reply; *reply=(TerminalReply){0}; }
void input_terminal_end(void) { capture=NULL; }
static bool report_frame(void) {
    /* Keep the historically guarded missing-? type-62 DA reply isolated too.
       Other unknown CSI frames end at their ordinary final byte. */
    return used>=3 && frame[1]=='[' && (frame[2]=='?' || (used>=4 && frame[2]=='6' && frame[3]==';') ||
        (used>=5 && frame[2]=='6' && frame[3]=='2' && frame[4]==';'));
}
static bool mouse_number(const char **p,unsigned *value) {
    *value=0; bool digit=false;
    while(**p>='0'&&**p<='9') {
        digit=true; if(*value>3276) return false;
        *value=*value*10+(unsigned)(*(*p)++-'0');
    }
    return digit && *value<=32768;
}
static int sequence_key(WINDOW *win) {
    if(overflow) return 0;
    frame[used]=0;
    if(frame[1]=='[' && used>=3 && (frame[used-1]=='c'||frame[used-1]=='t')) {
        if(capture) terminal_report(frame,used,capture);
        return 0;
    }
    if(used>4 && frame[1]=='[' && frame[2]=='<') {
        unsigned b,x,y; const char *p=frame+3;
        if(!mouse_number(&p,&b)||*p++!=';'||!mouse_number(&p,&x)||*p++!=';'||
           !mouse_number(&p,&y)||!x||!y||!(*p=='M'||*p=='m')||p[1]) return 0;
        char end=*p;
        unsigned button=b&~(4u|8u|16u);
        if(button==128||button==129) return end=='M' ? (button==128?UI_BACK:UI_FORWARD) : 0;
        MEVENT e={.x=(int)x-1,.y=(int)y-1};
        if(button==0) e.bstate=end=='M'?BUTTON1_PRESSED:end=='m'?BUTTON1_RELEASED:0;
        else if(button==64&&end=='M') e.bstate=BUTTON4_PRESSED;
        else if(button==65&&end=='M') e.bstate=BUTTON5_PRESSED;
        if(!e.bstate) return 0;
        if(b&4) e.bstate|=BUTTON_SHIFT;
        if(b&8) e.bstate|=BUTTON_ALT;
        if(b&16) e.bstate|=BUTTON_CTRL;
        if(ungetmouse(&e)==OK) return wgetch(win);
        return 0;
    }
    int key=key_defined(frame);
    /* Both CSI and SS3 cursor keys are common even when terminfo advertises one. */
    if(!key && used==3 && (frame[1]=='['||frame[1]=='O')) {
        switch(frame[2]) {
        case 'A': key=KEY_UP; break; case 'B': key=KEY_DOWN; break;
        case 'C': key=KEY_RIGHT; break; case 'D': key=KEY_LEFT; break;
        case 'H': key=KEY_HOME; break; case 'F': key=KEY_END; break;
        }
    }
    return key>0 ? key : 0; /* Unknown control sequences never become text. */
}
int terminal_input_wide(WINDOW *win,wint_t *key) {
    int original=wgetdelay(win);
    uint64_t deadline=core_monotonic_ms()+(original<0?25u:(unsigned)original);
    keypad(win,FALSE);
    for(unsigned work=0;work<256;work++) {
        if(work && core_monotonic_ms()>=deadline) { keypad(win,TRUE); return ERR; }
        wint_t c=0; int kind;
        if(has_queued) { c=queued; has_queued=false; kind=OK; }
        else {
            /* Only an undecided Esc or incomplete UTF-8 needs a timer.
               A retained late-reply guard must not wake an idle UI. */
            if((used==1 && !esc_delivered)||utf_remaining) {
                uint64_t now=core_monotonic_ms();
                unsigned remaining=now<deadline?(unsigned)(deadline-now):0;
                wtimeout(win,remaining<25 ? (int)remaining : 25);
            }
            kind=raw_wide(win,&c);
            wtimeout(win,original);
        }
        if(kind==4) continue;
        if(kind==3) { frame[0]=27;frame[1]='[';used=2;overflow=false;report_escape=false;continue; }
        if(kind==KEY_CODE_YES) { *key=c; keypad(win,TRUE); return kind; }
        if(kind==ERR) {
            keypad(win,TRUE);
            if(used==1 && !esc_delivered && core_monotonic_ms()-escape_started>=(capture?100u:25u)) { esc_delivered=true; *key=27; return OK; }
            return ERR;
        }
        if(!used) {
            if(c!=27) { *key=c; keypad(win,TRUE); return OK; }
            frame[0]=27; used=1; escape_started=core_monotonic_ms(); esc_delivered=false; overflow=false; report_escape=false; continue;
        }
        if(used==1) {
            if(c==27) { escape_started=core_monotonic_ms(); esc_delivered=false; continue; }
            if(c=='['||c=='O') { frame[used++]=(char)c; continue; }
            used=0;
            if(esc_delivered) { *key=c; keypad(win,TRUE); return OK; }
            queued=c; has_queued=true; *key=27; keypad(win,TRUE); return OK;
        }
        if(report_escape) {
            if(c==27) continue; /* Repeated Esc keeps the recovery prefix. */
            report_escape=false;
            if(c=='['||c=='O') {
                frame[0]=27; frame[1]=(char)c; used=2; overflow=false;
                continue; /* A fresh framed sequence is an explicit recovery boundary. */
            }
        }
        bool report=report_frame();
        if(c==27) {
            if(report||frame[1]=='[') { overflow=true; report_escape=true; *key=27; keypad(win,TRUE); return OK; }
            frame[0]=27; used=1; escape_started=core_monotonic_ms(); esc_delivered=false; overflow=false; report_escape=false; continue;
        }
        bool final=report ? c==((frame[2]=='?' || (used>=5 && frame[3]=='2'))?'c':'t') : c>=0x40&&c<=0x7e;
        if(used<128 && c<128) frame[used++]=(char)c; else overflow=true;
        if(!final) continue;
        int decoded=sequence_key(win);
        used=0; overflow=false; report_escape=false;
        if(decoded) { *key=(wint_t)decoded; keypad(win,TRUE); return KEY_CODE_YES; }
    }
    keypad(win,TRUE); return ERR; /* Bound work for hostile/noisy streams. */
}
