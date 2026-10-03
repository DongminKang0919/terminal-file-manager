#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include "../src/platform/platform.h"
#include <assert.h>
#include <unistd.h>
#include <utime.h>
static size_t reads, opens, live, bytes;
static bool fail_read, fail_refresh, forbid_list;
static size_t checks;
Result __real_app_refresh(AppState *);
Result __wrap_app_refresh(AppState *app) {
    return fail_refresh ? result_make(RESULT_ACCESS,"Injected refresh failure") : __real_app_refresh(app);
}
Result __real_platform_directory_open(const char *, PlatformDirectory **);
Result __wrap_platform_directory_open(const char *path, PlatformDirectory **out) {
    assert(!forbid_list); return __real_platform_directory_open(path,out);
}
Result __real_platform_info(const char *, FileInfo *);
Result __wrap_platform_info(const char *path, FileInfo *out) {
    assert(!forbid_list); return __real_platform_info(path,out);
}
Result __real_platform_reader_changed(PlatformReader *,const char *,bool *);
Result __wrap_platform_reader_changed(PlatformReader *reader,const char *path,bool *changed) {
    checks++; return __real_platform_reader_changed(reader,path,changed);
}
Result __real_platform_reader_open(const char *, PlatformReader **);
Result __real_platform_reader_line(PlatformReader *, char *, size_t, bool *);
void __real_platform_reader_close(PlatformReader *);
Result __wrap_platform_reader_open(const char *p, PlatformReader **r) {
    Result x = __real_platform_reader_open(p, r); opens++; if (*r) live++; return x;
}
Result __wrap_platform_reader_line(PlatformReader *r, char *b, size_t n, bool *end) {
    if (fail_read) return result_make(RESULT_IO, "Injected read failure");
    reads++; Result x = __real_platform_reader_line(r,b,n,end);
    if (x.code == RESULT_OK && !*end) bytes += strlen(b);
    return x;
}
void __wrap_platform_reader_close(PlatformReader *r) {
    if (r) { assert(live); live--; } __real_platform_reader_close(r);
}
static void write_file(const char *p, size_t n) {
    FILE *f = fopen(p,"w"); assert(f);
    for (size_t i=0;i<n;i++) fprintf(f,"line-%06zu\n",i);
    assert(!fclose(f));
}
int main(void) {
    char dir[]="/tmp/tfile-preview-XXXXXX"; assert(mkdtemp(dir));
    char path[256], other[256]; snprintf(path,sizeof path,"%s/file",dir); snprintf(other,sizeof other,"%s/new",dir);
    write_file(path,10000); PreviewSession *s=NULL; PreviewText p;
    assert(preview_session_open(path,&s).code==RESULT_OK);
    for(size_t i=0;i<1000;i++) {
        assert(preview_session_page(s,i,20,&p).code==RESULT_OK);
        char want[32]; snprintf(want,sizeof want,"line-%06zu",i);
        assert(p.len==20 && !strcmp(p.lines[0],want)); preview_text_free(&p);
    }
    assert(opens==1 && reads==1020 && bytes==12240);
    size_t saved=reads;
    assert(preview_session_page(s,990,20,&p).code==RESULT_OK); preview_text_free(&p); assert(reads==saved);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK); assert(!strcmp(p.lines[0],"line-000000")); preview_text_free(&p); assert(opens==2);
    assert(preview_session_page(s,20000,20,&p).code==RESULT_OK); assert(!p.len && p.skipped==10000 && !p.more); preview_text_free(&p);
    bool changed=false; write_file(path,2);
    assert(preview_session_check(s,&changed).code==RESULT_OK && changed);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK && p.len==2 && !p.more); preview_text_free(&p);
    write_file(other,3); assert(!rename(other,path));
    assert(preview_session_check(s,&changed).code==RESULT_OK && changed);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK && p.len==3); preview_text_free(&p);
    FILE *edit=fopen(path,"r+"); assert(edit); fputs("EDIT",edit); fclose(edit);
    struct utimbuf stamp = { .actime = 123456789, .modtime = 123456789 };
    assert(!utime(path, &stamp)); /* Deterministic even on coarse timestamp filesystems. */
    assert(preview_session_check(s,&changed).code==RESULT_OK && changed);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK && !strncmp(p.lines[0],"EDIT",4)); preview_text_free(&p);
    assert(!unlink(path)); assert(preview_session_check(s,&changed).code!=RESULT_OK); assert(live==0);
    saved=opens; assert(preview_session_page(s,0,20,&p).code!=RESULT_OK && opens==saved);
    preview_session_close(s);
    write_file(path,0); assert(preview_session_open(path,&s).code==RESULT_OK);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK && !p.len && !p.more); preview_text_free(&p); preview_session_close(s);
    FILE *f=fopen(path,"w"); assert(f); for(int i=0;i<10000;i++) fputc('x',f); fputc('\n',f); fclose(f);
    assert(preview_session_open(path,&s).code==RESULT_OK);
    assert(preview_session_page(s,0,20,&p).code==RESULT_OK && p.len==3 && strlen(p.lines[0])==4175); preview_text_free(&p); preview_session_close(s);
    f=fopen(path,"w"); assert(f); fputc(0,f); fclose(f);
    assert(preview_session_open(path,&s).code==RESULT_OK); assert(preview_session_page(s,0,20,&p).code==RESULT_OK && p.binary); preview_text_free(&p); preview_session_close(s);
    write_file(path,1000);
    assert(preview_session_open(path,&s).code==RESULT_OK); fail_read=true;
    assert(preview_session_page(s,0,20,&p).code==RESULT_IO && live==0);
    fail_read=false; preview_session_close(s);
    const char *special[]={"zA","za","z\n","z\\n","z\xff","z\\xFF","z한글"};
    char *extras[40];
    for (size_t i=0;i<40;i++) {
        char name[32]; snprintf(name,sizeof name,"z%zu",i);
        extras[i]=core_path_join(dir,i<7 ? special[i] : name); assert(extras[i]); write_file(extras[i],0);
    }
    UiContext ui={0}; assert(ui_init(&ui,dir).code==RESULT_OK); ui.show_preview=true;
    FILE *out=tmpfile(), *in=tmpfile(); assert(out && in); SCREEN *screen=newterm("xterm-256color",out,in); assert(screen);
    preview_prepare(&ui,17); saved=reads; size_t saved_opens=opens;
    for(int i=0;i<100;i++) { preview_prepare(&ui,17); preview(&ui,50,2,50,20); }
    assert(reads==saved && opens==saved_opens);
    ui.preview_checked=0; preview_prepare(&ui,17);
    assert(reads==saved && opens==saved_opens); /* Metadata poll does not read contents. */
    ui.preview_offset=200; preview_prepare(&ui,17);
    PreviewSession *session=ui.preview_session; char **page=ui.preview_page.lines;
    saved=reads; saved_opens=opens; size_t history=ui_panel(&ui)->app.history_len, checked=checks;
    forbid_list=true;
    for(int key=0;key<4;key++) for(int down=0;down<2;down++) {
        change_sort(&ui,(SortSettings){key,down});
        assert(!strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].path,path));
        assert(ui_panel(&ui)->top<=ui_panel(&ui)->selected && ui_panel(&ui)->selected<ui_panel(&ui)->top+17);
        ui.preview_checked=0; preview_prepare(&ui,17);
        assert(ui.preview_session==session && ui.preview_page.lines==page && ui.preview_offset==200);
        assert(reads==saved && opens==saved_opens && ui_panel(&ui)->app.history_len==history);
    }
    forbid_list=false; assert(checks>checked); /* Metadata polling is allowed, content I/O is not. */
    fail_refresh=true; size_t selected=ui_panel(&ui)->selected, top=ui_panel(&ui)->top;
    assert(load_dir(&ui,NULL).code==RESULT_ACCESS);
    assert(ui_panel(&ui)->selected==selected && ui_panel(&ui)->top==top && ui.preview_session==session && ui.preview_page.lines==page);
    fail_refresh=false;
    assert(load_dir(&ui,NULL).code==RESULT_OK && !strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].path,path));
    assert(!ui.preview_session && !ui.preview_page.lines && ui.preview_offset==0);
    preview_prepare(&ui,17); assert(opens==saved_opens+1);
    assert(load_dir(&ui,"zA").code==RESULT_OK && !strcmp(ui_panel(&ui)->app.files.entries[ui_panel(&ui)->selected].name,"zA"));
    selected=ui_panel(&ui)->selected; assert(!unlink(extras[0]));
    assert(load_dir(&ui,NULL).code==RESULT_OK);
    assert(ui_panel(&ui)->selected==(selected<ui_panel(&ui)->app.files.len ? selected : ui_panel(&ui)->app.files.len-1));
    write_file(extras[0],0); assert(load_dir(&ui,"file").code==RESULT_OK);
    ui.preview_offset=900; preview_prepare(&ui,17);
    write_file(path,2); preview_prepare(&ui,18);
    assert(ui.preview_offset==0 && ui.preview_page.len==2 && !ui.preview_page.more);
    assert(!unlink(path)); ui.preview_checked=0; preview_prepare(&ui,18);
    assert(ui.preview_result.code!=RESULT_OK && live==0 && ui.preview_offset==0);
    saved_opens=opens; preview_prepare(&ui,18); assert(opens==saved_opens);
    write_file(path,1000); preview_reset(&ui); preview_prepare(&ui,17);
    assert(ui.preview_result.code==RESULT_OK && live==1);
    for(int i=0;i<100;i++) { preview_reset(&ui); preview_prepare(&ui,17); assert(live==1); }
    ui.show_preview=false; preview_prepare(&ui,17); assert(live==0);
    ui.show_preview=true; preview_prepare(&ui,2); preview_prepare(&ui,30); assert(live==1);
    init_theme(); resizeterm(24,100); ui_panel(&ui)->selected=0; ui_panel(&ui)->top=0; draw(&ui);
    assert((mvinch(5,1)&A_CHARTEXT)=='>');
    assert(mvinch(5,3)&A_BOLD);
    assert(PAIR_NUMBER(mvinch(5,3))==UI_SELECTED);
    assert(mvinch(0,2)&A_BOLD);
    assert(!(mvinch(0,5)&A_BOLD));
    saved=reads; saved_opens=opens; checked=checks;
    session=ui.preview_session; page=ui.preview_page.lines;
    size_t saved_offset=ui.preview_offset, saved_selected=ui_panel(&ui)->selected, saved_top=ui_panel(&ui)->top;
    forbid_list=true; ui.preview_checked=0;
    WINDOW *modal=dialog_open(&ui,"Search",7,60); assert(modal);
    assert(ui.modal_depth==1 && !(mvinch(5,3)&A_BOLD));
    assert(PAIR_NUMBER(mvinch(5,3))==UI_INACTIVE);
    assert((mvinch(23,1)&A_CHARTEXT)==' ');
    for (int y=0;y<24;y++) for (int x=0;x<100;x++) {
        chtype c=mvinch(y,x);
        assert(PAIR_NUMBER(c)==UI_INACTIVE && (c&A_DIM) && !(c&(A_BOLD|A_REVERSE)));
    }
    WINDOW *nested=dialog_open(&ui,"Nested",7,50); assert(nested);
    dialog_close(&ui,nested); assert(ui.modal_depth==1 && !(mvinch(5,3)&A_BOLD));
    char term[256]="needle";
    unget_wch('\n'); ungetch(KEY_BTAB);
    assert(!search_prompt(&ui,modal,term,sizeof term));
    assert(mvwinch(modal,5,14)&A_BOLD); /* Shift-Tab reaches Cancel */
    unget_wch('\n'); unget_wch('\t'); unget_wch('\t');
    assert(!search_prompt(&ui,modal,term,sizeof term));
    assert(mvwinch(modal,5,14)&A_BOLD); /* Cancel focus */
    char hint[40]; mvwinnstr(modal,4,2,hint,12); assert(!strncmp(hint,"Enter: close",12));
    assert(!(mvwinch(modal,5,2)&A_BOLD) && !(mvwinch(modal,3,2)&A_BOLD));
    unget_wch('\n'); unget_wch('\t');
    assert(search_prompt(&ui,modal,term,sizeof term) && !strcmp(term,"needle"));
    assert(mvwinch(modal,5,2)&A_BOLD); /* Search focus */
    unget_wch('\n'); assert(search_prompt(&ui,modal,term,sizeof term));
    assert(mvwinch(modal,3,2)&A_BOLD); /* Input focus */
    dialog_close(&ui,modal); assert(ui.modal_depth==0 && (mvinch(5,3)&A_BOLD));
    assert(reads==saved && opens==saved_opens && checks==checked);
    assert(ui.preview_session==session && ui.preview_page.lines==page);
    assert(ui.preview_offset==saved_offset && ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top);
    /* Focus-only changes, Enter on this same file, and modal restore do not
       recreate the session or read the body. Normal metadata checks may run. */
    for (int i=0;i<20;i++) {
        enter_item(&ui); assert(ui.focus==UI_FOCUS_PREVIEW);
        draw(&ui);
        assert(mvinch(2,55)&A_BOLD); /* active preview border */
        assert(!(mvinch(2,0)&A_BOLD));
        assert((mvinch(2,57)&A_CHARTEXT)=='*');
        assert(panel_key(&ui,KEY_END,24));
        assert(ui.preview_offset==saved_offset);
        assert(panel_key(&ui,'\t',24) && ui.focus==UI_FOCUS_FILES);
        draw_cached(&ui);
        assert(panel_key(&ui,KEY_BTAB,24) && ui.focus==UI_FOCUS_PREVIEW);
        WINDOW *popup=dialog_open(&ui,"Focus",7,50); assert(popup);
        dialog_close(&ui,popup); assert(ui.focus==UI_FOCUS_PREVIEW);
        assert(panel_key(&ui,27,24) && ui.focus==UI_FOCUS_FILES);
        assert(ui.preview_session==session && ui.preview_page.lines==page);
        assert(ui.preview_offset==saved_offset && ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top);
        assert(reads==saved && opens==saved_opens);
    }
    checked=checks;
    Result nr=result_make(RESULT_CANCELLED,"Important partial result"); nr.partial=true;
    notice_record(&ui,"Copy",nr,"/long/한글\033/path",NULL);
    ui.focus=UI_FOCUS_PREVIEW;
    ungetch(27); ungetch(KEY_HOME); ungetch(KEY_END); ungetch(KEY_NPAGE); show_result(&ui);
    assert(ui.notice.visible && ui.focus==UI_FOCUS_PREVIEW);
    assert(reads==saved && opens==saved_opens && checks==checked);
    assert(ui.preview_session==session && ui.preview_page.lines==page);
    assert(ui.preview_offset==saved_offset && ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top);
    ungetch('a'); show_result(&ui); assert(!ui.notice.visible && ui.notice.present);
    ungetch(27); show_result(&ui);
    assert(reads==saved && opens==saved_opens && checks==checked);
    snprintf(ui.status,sizeof ui.status,"Important partial result");
    unget_wch('\n'); unget_wch('e'); unget_wch('l'); unget_wch('i'); unget_wch('f');
    quick_find(&ui);
    assert(ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top);
    assert(reads==saved && opens==saved_opens && ui.preview_session==session && ui.preview_page.lines==page);
    assert(!strcmp(ui.status,"Important partial result"));
    unget_wch(27); unget_wch('!'); quick_find(&ui);
    assert(ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top && reads==saved && opens==saved_opens);
    enter_item(&ui); assert(panel_key(&ui,KEY_DOWN,24)); draw(&ui);
    assert(ui.preview_offset==saved_offset+1);
    assert(panel_key(&ui,KEY_NPAGE,24)); draw(&ui);
    assert(ui.preview_offset==saved_offset+18);
    assert(ui_panel(&ui)->selected==saved_selected && ui_panel(&ui)->top==saved_top);
    saved=reads; assert(panel_key(&ui,KEY_HOME,24)); draw(&ui);
    assert(!ui.preview_offset && reads==saved); /* return within cache */
    saved=reads; saved_opens=opens; session=ui.preview_session; page=ui.preview_page.lines;
    ungetch(27); ungetch('\n'); ungetch('\n');
    ungetch(KEY_DOWN); ungetch(KEY_DOWN); ungetch(KEY_DOWN);
    show_options(&ui);
    assert(ui.focus==UI_FOCUS_PREVIEW && ui.preview_session==session && ui.preview_page.lines==page);
    assert(reads==saved && opens==saved_opens);
    Item *entries=ui_panel(&ui)->app.files.entries; bool hidden=ui_panel(&ui)->app.show_hidden;
    size_t option_selected=ui_panel(&ui)->selected, option_top=ui_panel(&ui)->top;
    fail_refresh=true; ungetch(27); ungetch('\n'); show_options(&ui); fail_refresh=false;
    assert(ui_panel(&ui)->app.show_hidden==hidden && ui_panel(&ui)->app.files.entries==entries);
    assert(ui_panel(&ui)->selected==option_selected && ui_panel(&ui)->top==option_top && strstr(ui.status,"Refresh failed"));
    assert(ui.preview_session==session && ui.preview_page.lines==page && reads==saved && opens==saved_opens);
    ui.show_preview=false; draw(&ui);
    assert(ui.focus==UI_FOCUS_FILES && !ui.preview_session && !live);
    enter_item(&ui); assert(ui.show_preview && ui.focus==UI_FOCUS_PREVIEW);
    draw(&ui); assert(live==1);
    forbid_list=false;
    short fg, bg, base_fg, base_bg;
    pair_content(UI_BASE,&base_fg,&base_bg);
    pair_content(UI_HEADER,&fg,&bg); assert(bg!=base_bg && fg!=bg);
    pair_content(UI_PATH,&fg,&bg); assert(bg==base_bg);
    pair_content(UI_SELECTED,&fg,&bg); assert(bg!=base_bg && fg!=bg);
    ui_free(&ui); assert(live==0);
    endwin(); delscreen(screen); fclose(out); fclose(in);
    out=tmpfile(); in=tmpfile(); assert(out && in);
    screen=newterm("vt100",out,in); assert(screen); init_theme();
    assert(!has_colors() && (ui_selection()&A_REVERSE));
    assert(ui_init(&ui,dir).code==RESULT_OK); ui_panel(&ui)->selected=0; ui_panel(&ui)->top=0;
    resizeterm(24,100); draw(&ui);
    assert((mvinch(5,1)&A_CHARTEXT)=='>'); assert(mvinch(5,3)&A_REVERSE);
    preview_reset(&ui); app_free(&ui_panel(&ui)->app);
    endwin(); delscreen(screen); fclose(out); fclose(in);
    for(size_t i=0;i<40;i++) { assert(!unlink(extras[i])); free(extras[i]); }
    assert(!unlink(path)); assert(!rmdir(dir));
    notice_clear(&ui);
    puts("PASS: 1000 sequential pages: 1 open, 1020 chunk reads, 12240 bytes; 100 unchanged redraws: 0 reads/opens; backward cache, EOF, changes, cleanup; sort preserves reader/page/selection without content or list I/O; refresh resets safely");
}
