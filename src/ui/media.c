#include "ui.h"
#include <limits.h>

static bool number(const char **p,const char *end,unsigned *out);

void graphics_init(UiContext *ui) {
    /* Optional explicit override; otherwise detection is lazy, session-only.
       TERM and the operating system never imply Sixel support. */
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
    ui->cell_width=ui->sixel_confirmed ? width : 0;
    ui->cell_height=ui->sixel_confirmed ? height : 0;
    ui->image_status=ui->sixel_confirmed ? IMAGE_ENABLED : IMAGE_UNCONFIRMED;
    ui->image_probe=ui->sixel_confirmed ? IMAGE_PROBE_DONE : IMAGE_PROBE_IDLE;
    ui->image_cells_fixed=ui->sixel_confirmed;
    if(stdscr) getmaxyx(stdscr,ui->image_rows,ui->image_columns);
}
void media_reset(UiContext *ui) {
    core_media_close(ui->media_job); ui->media_job=NULL;
    free(ui->media_data); ui->media_data=NULL; ui->media_len=0;
    ui->media_kind=PREVIEW_NOT_MEDIA; ui->media_done=false; ui->media_text=false; ui->media_fallback=false; ui->media_fallback_detail[0]=0;
    ui->media_width=ui->media_height=0;
    ui->media_output_width=ui->media_output_height=0;
    ui->media_target_width=ui->media_target_height=0; ui->media_resize_due=0;
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
bool graphics_inspect(const char *data,size_t len,unsigned max_width,unsigned max_height,
                      unsigned *output_width,unsigned *output_height) {
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
        pan!=1||pad!=1||!width||!height||width>max_width||height>max_height||width>PREVIEW_PIXEL_COUNT/height) return false;
    unsigned x=0,y=0; bool painted=false;
    while(p<end) {
        unsigned repeat=1; char c=*p++;
        if(c==27) {
            if(p>=end || *p++!='\\') return false;
            if(p!=end || !painted) return false;
            if(output_width) *output_width=width;
            if(output_height) *output_height=height;
            return true;
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
bool graphics_validate(const char *data,size_t len,unsigned width,unsigned height) {
    return graphics_inspect(data,len,width,height,NULL,NULL);
}
/* The text body begins below fixed metadata and its separator. Media reserves
   one additional cell on all sides, inside the borders and above the footer. */
UiMediaArea ui_media_area(int width,int height) {
    UiLayout layout=ui_layout(width,height,true);
    UiPreviewBody body=ui_preview_body(layout.panel_height);
    return (UiMediaArea){layout.list_width+2,2+body.top+1,width-layout.list_width-4,body.rows-2};
}
static void geometry(UiContext *ui,unsigned *width,unsigned *height) {
    int h=0,w=0; if(stdscr) getmaxyx(stdscr,h,w);
    UiMediaArea area=ui_media_area(w,h);
    *width=*height=0;
    if(h<9 || w<50 || area.columns<12 || area.rows<3 || !ui->sixel_confirmed || !ui->cell_width || !ui->cell_height) return;
    unsigned px=(unsigned)area.columns*ui->cell_width,py=(unsigned)area.rows*ui->cell_height;
    *width=px<PREVIEW_PIXEL_WIDTH?px:PREVIEW_PIXEL_WIDTH;
    *height=py<PREVIEW_PIXEL_HEIGHT?py:PREVIEW_PIXEL_HEIGHT;
    *height-=*height%6; /* Whole sixel bands stay inside the panel. */
}

void media_prepare(UiContext *ui,int rows,bool changed) {
    core_media_reap();
    PreviewMediaKind kind=preview_session_media(ui->preview_session);
    if(kind!=PREVIEW_NOT_MEDIA) graphics_probe_prepare(ui,rows);
    unsigned width,height; geometry(ui,&width,&height);
    bool text=kind==PREVIEW_PDF && (ui->media_fallback || !ui->image_auto || !ui->sixel_confirmed || !width || !height);
    bool size_changed=width!=ui->media_width || height!=ui->media_height;
    bool original_fits=kind!=PREVIEW_PDF && ui->media_done && ui->media_output_width &&
        ui->media_output_width+5<ui->media_width && ui->media_output_height+5<ui->media_height &&
        ui->media_output_width<=width && ui->media_output_height<=height;
    if(changed || kind!=ui->media_kind || text!=ui->media_text || !width || !height) {
        if(changed || kind!=ui->media_kind || size_changed || text!=ui->media_text) {
            media_reset(ui); text=kind==PREVIEW_PDF && (!ui->image_auto || !ui->sixel_confirmed || !width || !height);
            ui->media_kind=kind; ui->media_text=text;
            ui->media_width=width; ui->media_height=height;
        }
    } else if(size_changed && !text && !original_fits) {
        /* Cancel obsolete in-flight work immediately, but retain safe completed
           pixels until geometry settles for 120 ms. Position-only moves reuse. */
        uint64_t now=core_monotonic_ms();
        if(width!=ui->media_target_width || height!=ui->media_target_height || !ui->media_resize_due) {
            core_media_close(ui->media_job); ui->media_job=NULL;
            ui->media_target_width=width; ui->media_target_height=height;
            ui->media_resize_due=now+120;
        }
        if(now<ui->media_resize_due) return;
        media_reset(ui); ui->media_kind=kind; ui->media_text=text;
        ui->media_width=width; ui->media_height=height;
    } else {
        ui->media_resize_due=0; ui->media_target_width=ui->media_target_height=0;
    }
    if(ui->image_probe==IMAGE_PROBE_WAITING) {
        snprintf(ui->media_hint,sizeof ui->media_hint,"Checking terminal image support...");
        ui->media_done=false; return; /* No converter until capability/geometry is known. */
    }
    const char *failure=ui->image_status==IMAGE_UNSUPPORTED ? "Terminal has no Sixel; F7: image setup" :
        ui->image_status==IMAGE_NO_CELLS ? "Cell pixels unknown; F7: image setup" :
        ui->image_status==IMAGE_CANCELLED ? "Image query cancelled; F7: image setup" :
        ui->image_status==IMAGE_QUERY_FAILED ? "Terminal query failed; F7: image setup" :
        ui->image_status==IMAGE_NO_RESPONSE ? "No terminal reply; F7: image setup" :
        "Image display unconfirmed; set up in F7";
    const char *hint=!ui->image_auto?"Image preview off (F7: Auto)":!ui->sixel_confirmed?
        failure:!ui->cell_width||!ui->cell_height?failure:!width||!height?"Panel too small for image preview":NULL;
    snprintf(ui->media_hint,sizeof ui->media_hint,"%s",hint?hint:kind==PREVIEW_PDF?"PDF page 1":"Image preview");
    if(ui->media_fallback) snprintf(ui->media_hint,sizeof ui->media_hint,"%.127s",ui->media_fallback_detail);
    if(kind==PREVIEW_NOT_MEDIA || (hint && !text)) {
        if(kind!=PREVIEW_NOT_MEDIA && ui->image_auto && !ui->media_done && !terminal_tools().image)
            ui->media_result=result_make(RESULT_UNSUPPORTED,"Missing ImageMagick; install ImageMagick");
        ui->media_done=true; return;
    }
    if(ui->media_done || ui->modal_depth) return;
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
        !graphics_inspect(ui->media_data,ui->media_len,width,height,&ui->media_output_width,&ui->media_output_height)) {
        free(ui->media_data); ui->media_data=NULL; ui->media_len=0;
        ui->media_result=result_make(RESULT_IO,"Converter returned invalid or oversized Sixel output");
    }
}
bool media_pending(const UiContext *ui) {
    return ui->media_resize_due || ui->image_probe==IMAGE_PROBE_WAITING || (ui->media_kind!=PREVIEW_NOT_MEDIA && !ui->media_done);
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
    if(ui->image_probe==IMAGE_PROBE_WAITING || ui->modal_depth || !ui_preview_enabled(ui) || !ui->image_auto || !ui->sixel_confirmed ||
       !ui->media_done || ui->media_text || !ui->media_data || ui->media_result.code!=RESULT_OK || ui->preview_offset) return;
    if(!ui->preview_path || ui_panel(ui)->selected>=ui_panel(ui)->app.files.len ||
       strcmp(ui->preview_path,ui_panel(ui)->app.files.entries[ui_panel(ui)->selected].path)) return;
    int h,w; getmaxyx(stdscr,h,w);
    /* Cached modal restoration must wait for the resize preparation pass,
       even when both old/new pixel limits happen to hit the same caps. */
    if(ui->image_columns && (w!=ui->image_columns || h!=ui->image_rows)) return;
    UiMediaArea area=ui_media_area(w,h);
    unsigned width=ui->media_output_width,height=ui->media_output_height;
    if(w<50 || h<9 || !width || !height || !ui->cell_width || !ui->cell_height ||
       area.columns<12 || area.rows<3 || width>(unsigned)area.columns*ui->cell_width ||
       ((height+5)/6*6)>(unsigned)area.rows*ui->cell_height) return;
    int x=area.x+(int)(((unsigned)area.columns*ui->cell_width-width)/2/ui->cell_width);
    unsigned band_height=(height+5)/6*6;
    int y=area.y+(int)(((unsigned)area.rows*ui->cell_height-band_height)/2/ui->cell_height);
    /* Save/restore cursor and use cursor-relative Sixel placement;
       the validated image bounds prevent scrolling. */
    fprintf(stdout,"\0337\033[?80s\033[?80l\033[%d;%dH",y+1,x+1);
    fwrite(ui->media_data,1,ui->media_len,stdout);
    fputs("\033[?80r\0338",stdout); fflush(stdout); ui->graphics_visible=true;
}
