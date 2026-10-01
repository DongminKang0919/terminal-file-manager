#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>

typedef struct { wchar_t text[CCHARW_MAX]; attr_t attr; short pair; } Cell;
static Cell cell(WINDOW *w, int y, int x) {
    cchar_t c; Cell result={0};
    assert(mvwin_wch(w,y,x,&c)==OK);
    assert(getcchar(&c,result.text,&result.attr,&result.pair,NULL)==OK);
    return result;
}
static void same_text(Cell a, Cell b) { assert(!wcscmp(a.text,b.text)); }
int main(void) {
    assert(setlocale(LC_ALL,"C.UTF-8"));
    const char *terms[]={"xterm-256color","xterm","vt100"};
    const int widths[]={50,80,160}, heights[]={9,24,32};
    for (int t=0;t<3;t++) {
        FILE *out=tmpfile(), *in=tmpfile(); assert(out && in);
        SCREEN *screen=newterm(terms[t],out,in); assert(screen); init_theme();
        assert(t!=1 || COLORS==8); assert(t!=2 || !has_colors());
        for (int n=0;n<3;n++) {
            int h=heights[n], w=widths[n]; resizeterm(h,w);
            Item items[]={ {.name="한글-directory",.path="/a",.valid=true,.kind=FILE_DIRECTORY},
                           {.name="link",.path="/b",.valid=true,.kind=FILE_LINK} };
            UiContext ui={0}; ui.app.directory="/"; ui.app.files=(FileList){items,2};
            draw_cached(&ui);
            Cell *original=calloc((size_t)(h*w),sizeof *original); assert(original);
            for (int y=0;y<h;y++) for (int x=0;x<w;x++) original[y*w+x]=cell(stdscr,y,x);
            int pw=w<82?w-4:78, ph=h-2;
            WINDOW *win=dialog_open(&ui,"Popup 한글",ph,pw); assert(win);
            for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
                Cell c=cell(stdscr,y,x);
                if(y<h-1) same_text(c,original[y*w+x]);
                else assert(c.text[0]==L' '); /* Main command hints are hidden. */
                assert((c.attr&A_DIM) && !(c.attr&(A_BOLD|A_REVERSE)));
                assert(c.pair==(has_colors()?UI_INACTIVE:0));
            }
            draw_window_text(win,1,2,pw-4,"한글 /long/path/한글/filename\n\033[99;99H");
            dialog_button(win,ph-2,2,"[ One ]",false,true);
            dialog_button(win,ph-2,11,"[ Two ]",true,true);
            dialog_button(win,ph-2,20,"[ Off ]",false,false);
            Cell *before=calloc((size_t)(ph*pw),sizeof *before); assert(before);
            for(int y=0;y<ph;y++) for(int x=0;x<pw;x++) before[y*pw+x]=cell(win,y,x);
            dialog_refresh(win);
            for(int y=0;y<ph;y++) for(int x=0;x<pw;x++) { Cell now=cell(win,y,x); if(wcscmp(before[y*pw+x].text,now.text)) fprintf(stderr,"popup %s %dx%d at %d,%d: %ls != %ls\n",terms[t],w,h,y,x,before[y*pw+x].text,now.text); same_text(before[y*pw+x],now); }
            assert(cell(win,0,2).attr&A_BOLD);
            assert(cell(win,1,0).attr&A_BOLD);
            assert(cell(win,ph-3,1).attr&A_ALTCHARSET); /* footer rule */
            assert(!(cell(win,ph-2,2).attr&A_BOLD));
            assert(cell(win,ph-2,11).attr&A_BOLD);
            assert(cell(win,ph-2,20).attr&A_DIM);
            if(has_colors()) {
                assert(cell(win,0,2).pair==UI_POP_TITLE);
                assert(cell(win,1,2).pair==UI_POP_BODY);
                assert(cell(win,ph-3,1).pair==UI_POP_FOOTER);
                assert(cell(win,ph-2,11).pair==UI_SELECTED);
                short fg,bg,main_fg,main_bg;
                pair_content(UI_BASE,&main_fg,&main_bg); pair_content(UI_POP_BODY,&fg,&bg);
                assert(bg!=main_bg && fg!=bg);
                pair_content(UI_POP_BORDER,&fg,&bg);
                if(COLORS>=256) { short inactive, ibg; pair_content(UI_INACTIVE,&inactive,&ibg); assert(fg>inactive); }
            } else {
                assert(cell(win,0,2).attr&A_REVERSE);
                assert(cell(win,ph-2,11).attr&A_REVERSE);
            }
            dialog_close(&ui,win); assert(!ui.modal_depth);
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
                Cell restored=cell(stdscr,y,x), saved=original[y*w+x];
                same_text(restored,saved); assert(restored.attr==saved.attr && restored.pair==saved.pair);
            }
            free(before); free(original);
        }
        endwin(); delscreen(screen); fclose(in); fclose(out);
    }
    puts("PASS: 256/8/mono popup surfaces, title/border/footer/focus/disabled attributes, wide glyph preservation and exact background restoration at 50x9/80x24/160x32");
}
