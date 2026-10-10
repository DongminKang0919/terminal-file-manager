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
    const int widths[]={50,80,100,160}, heights[]={9,24,24,32};
    for (int t=0;t<3;t++) {
        FILE *out=tmpfile(), *in=tmpfile(); assert(out && in);
        SCREEN *screen=newterm(terms[t],out,in); assert(screen); init_theme();
        assert(t!=1 || COLORS==8); assert(t!=2 || !has_colors());
        for (int n=0;n<4;n++) {
            int h=heights[n], w=widths[n]; resizeterm(h,w);
            Item items[]={ {.name="한글-directory",.path="/a",.valid=true,.kind=FILE_DIRECTORY},
                           {.name="link",.path="/b",.valid=true,.kind=FILE_LINK} };
            UiContext ui={0}; ui_panel(&ui)->app.directory="/"; ui_panel(&ui)->app.files=(FileList){items,2};
            draw_cached(&ui);
            UiLayout layout=ui_layout(w,h,false);
            assert(cell(stdscr,0,2).attr&A_BOLD);
            assert(!(cell(stdscr,0,1).attr&A_DIM));
            assert(cell(stdscr,2,4).attr&A_BOLD);
            assert(!(cell(stdscr,2,10).attr&A_BOLD));
            assert(cell(stdscr,1,1).attr&A_DIM);
            assert(cell(stdscr,1,5).attr&A_DIM);
            assert(cell(stdscr,h-1,1).attr&A_BOLD);
            assert(!(cell(stdscr,h-1,6).attr&A_BOLD));
            assert(cell(stdscr,layout.list_y,1).text[0]==L'>');
            assert(cell(stdscr,layout.list_y,3).attr&A_BOLD);
            if(has_colors()) {
                assert(cell(stdscr,0,2).pair==UI_COMMAND);
                assert(cell(stdscr,2,1).pair==UI_FOCUS);
                assert(cell(stdscr,0,w-1).pair==UI_HEADER);
                assert(cell(stdscr,h-1,w-1).pair==UI_HEADER);
                assert(cell(stdscr,1,10).pair==UI_MUTED);
                assert(cell(stdscr,1,20).pair==UI_PATH);
                assert(cell(stdscr,layout.list_y,3).pair==UI_SELECTED);
                assert(cell(stdscr,layout.list_y,w-3).pair==UI_SELECTED);
                assert(cell(stdscr,layout.list_y+1,3).pair==UI_LINK);
                assert(cell(stdscr,layout.list_y+1,(w>=72?w-18:w-12)).pair==UI_MUTED);
                if(layout.columns) assert(cell(stdscr,4,3).pair==UI_COLUMNS);
                short fg,bg,bfg,bbg;
                pair_content(UI_HEADER,&fg,&bg); pair_content(UI_BASE,&bfg,&bbg);
                assert(fg!=bg);
                if(COLORS>=256) assert(bg!=bbg);
            } else {
                assert(!(cell(stdscr,0,1).attr&A_REVERSE));
                assert(!(cell(stdscr,h-1,1).attr&A_REVERSE));
                assert(cell(stdscr,layout.list_y,1).attr&A_REVERSE);
                if(layout.columns) assert(cell(stdscr,4,3).attr&A_UNDERLINE);
            }
            /* Every rendered button cell, and only those cells, is clickable. */
            int key=0;
            const int wide_keys[]={1,3,2,5,6,8,7,9,10};
            const int medium_keys[]={1,3,5,6,8,9,10};
            const int narrow_keys[]={1,3,9,10};
            const int *fkeys=w<80?narrow_keys:w<100?medium_keys:wide_keys;
            size_t expected=w<80?4:w<100?7:9;
            size_t action=0;
            for(int x=0;x<w;x++) {
                wchar_t ch=cell(stdscr,0,x).text[0];
                if(ch==L'[') { assert(action<expected); key=KEY_F(fkeys[action++]); }
                assert(header_action(x,w)==key);
                if(ch==L']') key=0;
            }
            assert(action==expected);
            /* Marks remain distinct on a selected hidden item and after moving. */
            items[0].hidden=true;
            assert(app_mark_toggle(&ui_panel(&ui)->app,items[0].name).code==RESULT_OK);
            draw_cached(&ui);
            assert(cell(stdscr,layout.list_y,2).text[0]==L'*');
            assert(cell(stdscr,layout.list_y,2).attr&A_BOLD);
            if(has_colors()) {
                assert(cell(stdscr,layout.list_y,2).pair==UI_MARK_SELECTED);
                assert(cell(stdscr,layout.list_y,3).pair==UI_SELECTED);
            }
            ui_panel(&ui)->selected=1; draw_cached(&ui);
            assert(cell(stdscr,layout.list_y,1).text[0]==L' ');
            assert(cell(stdscr,layout.list_y,2).text[0]==L'*');
            if(has_colors()) assert(cell(stdscr,layout.list_y,2).pair==UI_MARK);
            app_marks_clear(&ui_panel(&ui)->app);
            ui_panel(&ui)->selected=0; items[0].hidden=false; draw_cached(&ui);
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
            int wy,wx; getbegyx(win,wy,wx);
            for(int x=0;x<pw;x++) {
                MEVENT click={.x=wx+x,.y=wy+ph-2,.bstate=BUTTON1_PRESSED};
                assert(dialog_button_hit(win,&click,ph-2,11,"[ Two ]",true)==(x>=11 && x<18));
                assert(!dialog_button_hit(win,&click,ph-2,20,"[ Off ]",false));
            }
            /* Clipped wide labels include the ellipsis but never its blank gap. */
            const char *wide="한글한글한글";
            int available=6, at=pw-2-available;
            MEVENT edge={.x=wx+at+4,.y=wy+ph-2,.bstate=BUTTON1_PRESSED};
            assert(dialog_button_hit(win,&edge,ph-2,at,wide,true));
            edge.x++; assert(!dialog_button_hit(win,&edge,ph-2,at,wide,true));
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
            if(has_colors()) {
                short fg,bg;
                pair_content(UI_COMMAND,&fg,&bg); assert(fg!=bg);
                pair_content(UI_MARK_SELECTED,&fg,&bg); assert(fg!=bg);
                pair_content(UI_POP_WARNING,&fg,&bg); assert(fg!=bg);
                if(COLORS==8) assert(fg==COLOR_BLACK && bg==COLOR_YELLOW);
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
    puts("PASS: 256/8/mono popup surfaces, title/border/footer/focus/disabled attributes, wide glyph preservation and exact background restoration at 50x9/80x24/100x24/160x32");
}
