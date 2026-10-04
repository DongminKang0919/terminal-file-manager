#include "ui.h"
#include <limits.h>

static bool number(const char **p,const char *end,unsigned *out);

void graphics_init(UiContext *ui) {
    /* No terminal queries: capability is explicitly asserted by the user.
       TERM and WSL never decide support, and no replies enter the key stream. */
    const char *confirmed=getenv("TFILE_SIXEL"), *cells=getenv("TFILE_CELL_PIXELS");
    unsigned width=0,height=0;
    bool tty=core_terminal_pixels(&width,&height);
    if(cells) {
        const char *p=cells,*end=cells+strlen(cells);
        if(!number(&p,end,&width) || p==end || *p++!='x' || !number(&p,end,&height) || p!=end)
            width=height=0;
    }
    ui->sixel_confirmed=tty && confirmed && !strcmp(confirmed,"1") &&
        width>0 && width<=64 && height>0 && height<=128;
    ui->cell_width=width; ui->cell_height=height;
}
void media_reset(UiContext *ui) {
    core_media_close(ui->media_job); ui->media_job=NULL;
    free(ui->media_data); ui->media_data=NULL; ui->media_len=0;
    ui->media_kind=PREVIEW_NOT_MEDIA; ui->media_done=false; ui->media_text=false; ui->media_fallback=false; ui->media_fallback_detail[0]=0;
    ui->media_width=ui->media_height=0;
    ui->media_result=result_make(RESULT_OK,NULL); ui->media_hint[0]=0;
}
static bool number(const char **p,const char *end,unsigned *out) {
    unsigned value=0; const char *start=*p;
    while(*p<end && **p>='0' && **p<='9') {
        if(value>100000u) return false;
        value=value*10+(unsigned)(*(*p)++-'0');
    }
    if(*p==start) return false;
    *out=value; return true;
}
static bool parameter(const char **p,const char *end,unsigned *out) {
    if(*p>=end || *(*p)++!=';') return false;
    return number(p,end,out);
}
/* Accept exactly one bounded, square-pixel Sixel DCS. Reject all surrounding
   controls, cursor commands, strings, extra images and raster overdraw. */
bool graphics_validate(const char *data,size_t len,unsigned max_width,unsigned max_height) {
    if(len<14 || len>PREVIEW_SIXEL_BYTES || data[0]!=27 || data[1]!='P') return false;
    const char *p=data+2,*end=data+len;
    unsigned param=0;
    if(p<end && *p!='q') {
        if(!number(&p,end,&param)||param!=0) return false;
        if(p<end && *p==';') {
            if(!parameter(&p,end,&param)||param>2) return false;
            if(p<end && *p==';' && (!parameter(&p,end,&param)||param!=0)) return false;
        }
    }
    if(p>=end || *p++!='q' || p>=end || *p++!='"') return false;
    unsigned pan,pad,width,height;
    if(!number(&p,end,&pan)||!parameter(&p,end,&pad)||!parameter(&p,end,&width)||!parameter(&p,end,&height)||
        pan!=1||pad!=1||!width||!height||width>max_width||height>max_height) return false;
    unsigned x=0,y=0; bool painted=false;
    while(p<end) {
        unsigned repeat=1; char c=*p++;
        if(c==27) {
            if(p>=end || *p++!='\\') return false;
            return p==end && painted;
        }
        if(c=='#') {
            unsigned color;
            if(!number(&p,end,&color)||color>255) return false;
            if(p<end && *p==';') {
                unsigned model,a,b,d;
                if(!parameter(&p,end,&model)||!parameter(&p,end,&a)||!parameter(&p,end,&b)||!parameter(&p,end,&d)||
                    model!=2||a>100||b>100||d>100) return false;
            }
            continue;
        }
        if(c=='$') { x=0; continue; }
        if(c=='-') { x=0; y+=6; if(y>height+5) return false; continue; }
        if(c=='!') {
            if(!number(&p,end,&repeat)||!repeat||p>=end) return false;
            c=*p++;
        }
        if(c<'?' || c>'~' || repeat>width || x>width-repeat) return false;
        unsigned bits=(unsigned)(c-'?');
        for(unsigned bit=0;bit<6;bit++) if(bits&(1u<<bit)) {
            if(y+bit>=height) return false;
            painted=true;
        }
        x+=repeat;
    }
    return false;
}
static void geometry(UiContext *ui,int rows,unsigned *width,unsigned *height) {
    int h=0,w=0; if(stdscr) getmaxyx(stdscr,h,w);
    UiLayout layout=ui_layout(w,h,true);
    int columns=w-layout.list_width-4;
    int image_rows=rows-PREVIEW_METADATA_ROWS-1;
    *width=*height=0;
    if(columns<12 || image_rows<3 || !ui->sixel_confirmed) return;
    unsigned px=(unsigned)columns*ui->cell_width,py=(unsigned)image_rows*ui->cell_height;
    *width=px<PREVIEW_PIXEL_WIDTH?px:PREVIEW_PIXEL_WIDTH;
    *height=py<PREVIEW_PIXEL_HEIGHT?py:PREVIEW_PIXEL_HEIGHT;
    *height-=*height%6; /* Whole sixel bands stay inside the panel. */
}
void media_prepare(UiContext *ui,int rows,bool changed) {
    core_media_reap();
    PreviewMediaKind kind=preview_session_media(ui->preview_session);
    unsigned width,height; geometry(ui,rows,&width,&height);
    bool text=kind==PREVIEW_PDF && (ui->media_fallback || !ui->image_auto || !ui->sixel_confirmed || !width || !height);
    if(changed || kind!=ui->media_kind || width!=ui->media_width || height!=ui->media_height || text!=ui->media_text) {
        media_reset(ui); text=kind==PREVIEW_PDF && (!ui->image_auto || !ui->sixel_confirmed || !width || !height);
        ui->media_kind=kind; ui->media_text=text;
        ui->media_width=width; ui->media_height=height;
    }
    const char *hint=!ui->image_auto?"Image preview off (F7: Auto)":!ui->sixel_confirmed?
        "Sixel not confirmed; see README":!width||!height?"Panel too small for image preview":NULL;
    snprintf(ui->media_hint,sizeof ui->media_hint,"%s",hint?hint:kind==PREVIEW_PDF?"PDF page 1":"Image preview");
    if(ui->media_fallback) snprintf(ui->media_hint,sizeof ui->media_hint,"%.127s",ui->media_fallback_detail);
    if(kind==PREVIEW_NOT_MEDIA || (hint && !text)) { ui->media_done=true; return; }
    if(ui->media_done) return;
    const Item *it=&ui_panel(ui)->app.files.entries[ui_panel(ui)->selected];
    if(!ui->media_job) {
        ui->media_result=core_media_open(it->path,kind,text,width,height,&ui->media_job);
        if(ui->media_result.code==RESULT_CANCELLED) return; /* Retired child: retry next tick. */
        if(ui->media_result.code!=RESULT_OK) {
            if(kind==PREVIEW_PDF && !text && ui->media_result.code==RESULT_UNSUPPORTED) {
                snprintf(ui->media_fallback_detail,sizeof ui->media_fallback_detail,"%s",ui->media_result.detail);
                ui->media_fallback=true; ui->media_text=true; return;
            }
            ui->media_done=true; return;
        }
    }
    bool done=false;
    ui->media_result=core_media_poll(ui->media_job,&done,&ui->media_data,&ui->media_len);
    if(!done) return;
    ui->media_done=true;
    core_media_close(ui->media_job); ui->media_job=NULL;
    if(kind==PREVIEW_PDF && !text && ui->media_result.code==RESULT_UNSUPPORTED) {
        snprintf(ui->media_fallback_detail,sizeof ui->media_fallback_detail,"%s",ui->media_result.detail);
        ui->media_fallback=true; ui->media_text=true; ui->media_done=false; return;
    }
    if(ui->media_result.code==RESULT_OK && !text &&
        !graphics_validate(ui->media_data,ui->media_len,width,height)) {
        free(ui->media_data); ui->media_data=NULL; ui->media_len=0;
        ui->media_result=result_make(RESULT_IO,"Converter returned invalid or oversized Sixel output");
    }
}
bool media_pending(const UiContext *ui) {
    return ui->media_kind!=PREVIEW_NOT_MEDIA && !ui->media_done;
}
/* Every main/cached frame uses the same ordering: erase previous graphics,
   force curses repaint, refresh cells, then emit graphics. ED2 is intentionally
   conservative; ncurses' cell model cannot track sixel pixels. */
void graphics_clear(UiContext *ui) {
    if(!ui->graphics_visible) return;
    fputs("\033[0m\033[2J\033[H",stdout); fflush(stdout);
    if(stdscr) clearok(stdscr,TRUE);
    ui->graphics_visible=false;
}
void graphics_present(UiContext *ui) {
    if(ui->modal_depth || !ui_preview_enabled(ui) || !ui->image_auto || !ui->sixel_confirmed ||
       !ui->media_done || ui->media_text || !ui->media_data || ui->media_result.code!=RESULT_OK || ui->preview_offset) return;
    if(!ui->preview_path || ui_panel(ui)->selected>=ui_panel(ui)->app.files.len ||
       strcmp(ui->preview_path,ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].path)) return;
    int h,w; getmaxyx(stdscr,h,w);
    unsigned width,height; geometry(ui,h-7,&width,&height);
    if(!width||!height||width!=ui->media_width||height!=ui->media_height) return;
    int x=ui_layout(w,h,true).list_width+2,y=3+PREVIEW_METADATA_ROWS+1;
    /* Save/restore cursor and use cursor-relative Sixel placement;
       the validated image bounds prevent scrolling. */
    fprintf(stdout,"\0337\033[?80s\033[?80l\033[%d;%dH",y+1,x+1);
    fwrite(ui->media_data,1,ui->media_len,stdout);
    fputs("\033[?80r\0338",stdout); fflush(stdout); ui->graphics_visible=true;
}
