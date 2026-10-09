#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
#include <string.h>

static Result bad(const char *s) { return result_make(RESULT_INVALID_NAME,s); }
void favorites_free(Favorites *s) {
    for(size_t i=0;i<s->len;i++) { free(s->entries[i].name); free(s->entries[i].directory); }
    free(s->entries); free(s->path); *s=(Favorites){0};
}
static int hex(unsigned char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
static char *decode(const char *p,size_t n,size_t limit) {
    if(!n || n%2 || n/2>limit) return NULL;
    char *s=malloc(n/2+1); if(!s) return NULL;
    for(size_t i=0;i<n;i+=2) {
        int a=hex((unsigned char)p[i]),b=hex((unsigned char)p[i+1]);
        if(a<0||b<0||!(a||b)) { free(s); return NULL; }
        s[i/2]=(char)(a*16+b);
    }
    s[n/2]=0; return s;
}
void favorites_load(Favorites *s) {
    *s=(Favorites){0}; s->load_result=platform_favorites_path(&s->path);
    if(s->load_result.code!=RESULT_OK) return;
    char data[FAVORITES_BYTES+1]; size_t n=0;
    Result r=platform_favorites_read(s->path,data,sizeof data,&n);
    if(r.code==RESULT_NOT_FOUND) { s->load_result=result_make(RESULT_OK,NULL); return; }
    Favorites candidate={0};
    if(r.code==RESULT_OK && (n<10 || memcmp(data,"version=1\n",10))) r=bad("Invalid favorites version/header");
    for(size_t at=10;r.code==RESULT_OK && at<n;) {
        size_t end=at,tab=at;
        while(end<n && data[end]!='\n') end++;
        while(tab<end && data[tab]!='\t') tab++;
        if(end==n || tab==end || candidate.len==FAVORITES_MAX) { r=bad("Invalid or oversized favorites data"); break; }
        char *name=decode(data+at,tab-at,FAVORITE_NAME_BYTES);
        char *path=decode(data+tab+1,end-tab-1,4095);
        if(!name || !path || !platform_path_absolute(path)) { free(name); free(path); r=bad("Invalid favorites name/path encoding"); break; }
        for(size_t i=0;i<candidate.len;i++) if(!strcmp(candidate.entries[i].directory,path)) r=bad("Duplicate favorite directory");
        Favorite *e=r.code==RESULT_OK ? realloc(candidate.entries,(candidate.len+1)*sizeof *e) : NULL;
        if(!e) { free(name); free(path); if(r.code==RESULT_OK) r=result_make(RESULT_NO_MEMORY,"Out of memory"); break; }
        candidate.entries=e; e[candidate.len++]=(Favorite){name,path}; at=end+1;
    }
    if(r.code==RESULT_OK) { s->entries=candidate.entries; s->len=candidate.len; }
    else favorites_free(&candidate);
    s->load_result=r;
}
static bool encode(char *data,size_t *n,const char *text,char separator) {
    static const char digits[]="0123456789abcdef";
    size_t len=strlen(text);
    if(*n>=FAVORITES_BYTES || len>(FAVORITES_BYTES-*n-1)/2) return false;
    for(size_t i=0;i<len;i++) { unsigned char c=(unsigned char)text[i]; data[(*n)++]=digits[c>>4]; data[(*n)++]=digits[c&15]; }
    data[(*n)++]=separator; return true;
}
static Result save(Favorites *s,Favorite *entries,size_t count) {
    if(s->load_result.code!=RESULT_OK || !s->path) return result_make(RESULT_ACCESS,"Favorites unavailable; repair unreadable/invalid favorites.conf first");
    char data[FAVORITES_BYTES]; memcpy(data,"version=1\n",10); size_t n=10;
    for(size_t i=0;i<count;i++) if(!encode(data,&n,entries[i].name,'\t') || !encode(data,&n,entries[i].directory,'\n')) return bad("Favorites exceed 64 KiB");
    Result r=platform_favorites_write(s->path,data,n);
    if(r.code==RESULT_OK) { free(s->entries); s->entries=entries; s->len=count; }
    return r;
}
static bool valid_name(const char *name) { return name && *name && strlen(name)<=FAVORITE_NAME_BYTES; }
Result favorites_add(Favorites *s,const char *directory,const char *name) {
    if(!valid_name(name)) return bad("Favorite name must be 1-128 bytes");
    if(s->len==FAVORITES_MAX) return bad("At most 64 favorites");
    char *resolved=NULL; Result r=core_resolve_directory(NULL,directory,&resolved);
    if(r.code!=RESULT_OK) return r;
    if(strlen(resolved)>4095) { free(resolved); return bad("Favorite path exceeds 4095 bytes"); }
    for(size_t i=0;i<s->len;i++) if(!strcmp(s->entries[i].directory,resolved)) { free(resolved); return result_make(RESULT_EXISTS,"Directory already bookmarked"); }
    char *label=text_copy(name); Favorite *e=malloc((s->len+1)*sizeof *e);
    if(!label||!e) { free(label); free(e); free(resolved); return result_make(RESULT_NO_MEMORY,"Out of memory"); }
    if(s->len) memcpy(e,s->entries,s->len*sizeof *e);
    e[s->len]=(Favorite){label,resolved}; r=save(s,e,s->len+1);
    if(r.code!=RESULT_OK) { free(label); free(resolved); free(e); }
    return r;
}
Result favorites_rename(Favorites *s,size_t index,const char *name) {
    if(index>=s->len) return result_make(RESULT_NOT_FOUND,"No favorite selected");
    if(!valid_name(name)) return bad("Favorite name must be 1-128 bytes");
    char *label=text_copy(name),*old=s->entries[index].name;
    Favorite *e=malloc(s->len*sizeof *e);
    if(!label||!e) { free(label); free(e); return result_make(RESULT_NO_MEMORY,"Out of memory"); }
    memcpy(e,s->entries,s->len*sizeof *e); e[index].name=label;
    Result r=save(s,e,s->len);
    if(r.code==RESULT_OK) free(old); else { free(label); free(e); }
    return r;
}
Result favorites_remove(Favorites *s,size_t index) {
    if(index>=s->len) return result_make(RESULT_NOT_FOUND,"No favorite selected");
    Favorite old=s->entries[index]; Favorite *e=malloc((s->len?s->len:1)*sizeof *e);
    if(!e) return result_make(RESULT_NO_MEMORY,"Out of memory");
    for(size_t i=0,j=0;i<s->len;i++) if(i!=index) e[j++]=s->entries[i];
    Result r=save(s,e,s->len-1);
    if(r.code==RESULT_OK) { free(old.name); free(old.directory); } else free(e);
    return r;
}
