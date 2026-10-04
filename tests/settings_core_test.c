#include "../src/core/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    StartupSettings defaults=settings_defaults(), out=defaults;
    assert(defaults.mode==1&&defaults.wheel_step==1&&defaults.show_hidden&&defaults.image_auto);
    /* A valid prefix must never escape a failed transaction. */
    const char *bad="version=1\nmode=2\nsort=3\nhidden=0\nimage=9\n";
    assert(settings_parse(bad,strlen(bad),&out).code!=RESULT_OK);
    assert(out.mode==defaults.mode&&out.sort.key==defaults.sort.key&&out.show_hidden==defaults.show_hidden);
    const char *keys[]={"mode","sort","descending","hidden","wheel","image"};
    for(unsigned k=0;k<6;k++) for(int v=-1;v<=10;v++) {
        char data[80]; int n=snprintf(data,sizeof data,"version=1\n%s=%d\n",keys[k],v);
        bool valid=k==0 ? v>=0&&v<=2 : k==1 ? v>=0&&v<=3 : k==4 ? v==1||v==3||v==5 : v==0||v==1;
        assert((settings_parse(data,(size_t)n,&out).code==RESULT_OK)==valid);
    }
    puts("PASS: curses-free settings defaults, full value ranges and transactional validation");
}
