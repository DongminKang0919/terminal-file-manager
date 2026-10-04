#include "core.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StartupSettings settings_defaults(void) {
    return (StartupSettings){.mode=1,.wheel_step=1,.show_hidden=true,.image_auto=true};
}
static Result invalid(const char *reason) { return result_make(RESULT_INVALID_NAME,reason); }
Result settings_parse(const char *data,size_t len,StartupSettings *out) {
    StartupSettings candidate=settings_defaults();
    const char *keys[]={"version","mode","sort","descending","hidden","wheel","image"};
    unsigned seen=0;
    if(len>4096) return invalid("Settings file exceeds 4096 bytes");
    for(size_t at=0;at<len;) {
        size_t end=at; while(end<len && data[end]!='\n') end++;
        if(end-at>128) return invalid("Settings line exceeds 128 bytes");
        char line[129]; memcpy(line,data+at,end-at); line[end-at]=0;
        if(memchr(data+at,0,end-at)) return invalid("NUL in settings");
        at=end<len?end+1:end;
        if(!*line || *line=='#') continue;
        char *eq=strchr(line,'=');
        if(!eq || eq==line || !eq[1] || strchr(eq+1,'=')) return invalid("Expected key=value");
        *eq++=0;
        for(char *p=line;*p;p++) if(!((*p>='a'&&*p<='z')||*p=='_')) return invalid("Invalid settings key");
        int key=-1; for(int i=0;i<7;i++) if(!strcmp(line,keys[i])) key=i;
        if(key<0) continue;
        if(seen&(1u<<key)) return invalid("Duplicate settings key");
        seen|=1u<<key;
        if(eq[1] || *eq<'0' || *eq>'9') return invalid("Invalid settings value");
        int value=*eq-'0';
        switch(key) {
        case 0: if(value!=1) return result_make(RESULT_UNSUPPORTED,"Unsupported settings version"); break;
        case 1: if(value>2) return invalid("Invalid screen mode"); candidate.mode=value; break;
        case 2: if(value>3) return invalid("Invalid sort key"); candidate.sort.key=(SortKey)value; break;
        case 3: if(value>1) return invalid("Invalid sort direction"); candidate.sort.descending=value; break;
        case 4: if(value>1) return invalid("Invalid hidden flag"); candidate.show_hidden=value; break;
        case 5: if(value!=1&&value!=3&&value!=5) return invalid("Invalid wheel step"); candidate.wheel_step=value; break;
        case 6: if(value>1) return invalid("Invalid image preference"); candidate.image_auto=value; break;
        }
    }
    if(!(seen&1)) return invalid("Missing settings version");
    *out=candidate; return result_make(RESULT_OK,NULL);
}
void settings_load(SettingsStore *store) {
    *store=(SettingsStore){.defaults=settings_defaults()};
    store->load_result=platform_settings_path(&store->path);
    if(store->load_result.code!=RESULT_OK) return;
    char data[4097]; size_t len=0;
    Result r=platform_settings_read(store->path,data,sizeof data,&len);
    if(r.code==RESULT_NOT_FOUND) { store->load_result=result_make(RESULT_OK,NULL); return; }
    if(r.code==RESULT_OK) r=settings_parse(data,len,&store->defaults);
    store->load_result=r; store->replace_required=r.code!=RESULT_OK;
}
Result settings_save(SettingsStore *store,const StartupSettings *s,bool replace) {
    if(!store->path) return store->load_result;
    if(store->replace_required&&!replace) return result_make(RESULT_EXISTS,"Confirm replacement of unreadable/invalid settings");
    char data[256];
    int n=snprintf(data,sizeof data,"version=1\nmode=%d\nsort=%d\ndescending=%d\nhidden=%d\nwheel=%d\nimage=%d\n",s->mode,s->sort.key,s->sort.descending,s->show_hidden,s->wheel_step,s->image_auto);
    StartupSettings checked;
    Result r=settings_parse(data,(size_t)n,&checked);
    if(r.code==RESULT_OK) r=platform_settings_write(store->path,data,(size_t)n);
    if(r.code==RESULT_OK) { store->defaults=checked; store->replace_required=false; store->load_result=r; }
    return r;
}
void settings_free(SettingsStore *store) { free(store->path); *store=(SettingsStore){0}; }
