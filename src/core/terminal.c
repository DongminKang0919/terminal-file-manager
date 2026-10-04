#include "terminal.h"
#include "../platform/platform.h"
static bool integer(const char **p,const char *end,unsigned *value) {
    *value=0; unsigned digits=0;
    while(*p<end && **p>='0' && **p<='9') {
        if(++digits>5) return false;
        *value=*value*10+(unsigned)(*(*p)++-'0');
    }
    return digits!=0;
}
bool terminal_report(const char *s,size_t n,TerminalReply *reply) {
    if(n<4||n>128||s[0]!=27||s[1]!='[') return false;
    const char *p=s+2,*end=s+n-1;
    if(*p=='?' && *end=='c') {
        p++; unsigned value,count=0; bool sixel=false;
        do {
            if(++count>32||!integer(&p,end,&value)) return false;
            if(count>1 && value==4) sixel=true; /* First field is terminal class. */
            if(p==end) break;
            if(*p++!=';') return false;
        } while(p<end);
        if(p!=end || end[-1]==';') return false;
        reply->da_received=true; reply->sixel=sixel; return true;
    }
    if(*end=='t') {
        unsigned kind,h,w;
        if(!integer(&p,end,&kind)||p==end||*p++!=';'||!integer(&p,end,&h)||p==end||*p++!=';'||
           !integer(&p,end,&w)||p!=end||kind!=6||w<1||w>64||h<1||h>128) return false;
        reply->cells_received=true; reply->width=w; reply->height=h; return true;
    }
    return false;
}
Result terminal_query(void) { return platform_terminal_query(); }
Result terminal_query_cells(void) { return platform_terminal_query_cells(); }
TerminalTools terminal_tools(void) { return platform_terminal_tools(); }
