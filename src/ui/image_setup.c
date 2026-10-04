#include "ui.h"

const char *image_status_label(const UiContext *ui) {
    if(ui->image_status==IMAGE_CHECKING) return "Checking";
    if(!ui->image_auto) return "Off";
    if(ui->sixel_confirmed && ui->cell_width && ui->cell_height) return "Enabled";
    if(ui->image_status==IMAGE_NO_RESPONSE) return "No support response";
    if(ui->image_status==IMAGE_NO_CELLS) return "Cell size unknown";
    if(ui->image_status==IMAGE_UNSUPPORTED) return "No Sixel support";
    if(ui->image_status==IMAGE_QUERY_FAILED) return "Query failed";
    if(ui->image_status==IMAGE_CANCELLED) return "Query cancelled";
    return "Unconfirmed";
}
static void page(WINDOW *win,const char *question,const char *detail,const char *help) {
    int h=getmaxy(win),w=getmaxx(win);
    dialog_frame(win,"Image display setup");
    draw_window_text(win,1,2,w-4,question);
    draw_window_text(win,2,2,w-4,detail);
    draw_window_text(win,h-2,2,w-4,help);
    dialog_refresh(win);
}
static bool dimensions(const char *s,unsigned *w,unsigned *h) {
    unsigned a=0,b=0; size_t at=0;
    for(;s[at]>='0'&&s[at]<='9';at++) { if(at>=3) return false; a=a*10+(unsigned)(s[at]-'0'); }
    if(!at || s[at++]!='x') return false;
    size_t start=at;
    for(;s[at]>='0'&&s[at]<='9';at++) { if(at-start>=3) return false; b=b*10+(unsigned)(s[at]-'0'); }
    if(at==start || s[at] || !a||a>64||!b||b>128) return false;
    *w=a; *h=b; return true;
}
/* Returns true on resize so the old parent popup is closed too. */
bool image_setup(UiContext *ui,char *result,size_t capacity) {
    WINDOW *win=dialog_open(ui,"Image display setup",18,70);
    if(!win) { snprintf(result,capacity,"Resize terminal to at least 50x9"); return false; }
    TerminalTools tools=terminal_tools();
    TerminalReply candidate={0};
    bool resized=false, finished=false, showing=false;
    int stage=0, previous_status=ui->image_status;
    char detail[160]="", hint[160]="Executable discovery only; coders not tested";
    if(!tools.pdf_image||!tools.pdf_text) snprintf(hint,sizeof hint,"Missing Poppler tools; install poppler-utils");
    snprintf(result,capacity,"Image check cancelled; previous settings kept");
    for(;;) {
        int h=getmaxy(win),w=getmaxx(win);
        if(stage==0) {
            snprintf(detail,sizeof detail,"Image tool: %s",tools.image==1?"magick (found, not tested)":tools.image==2?"convert (found, not tested)":"missing; install ImageMagick");
            char title[100]; snprintf(title,sizeof title,"Image display: %s",image_status_label(ui));
            if(ui->sixel_confirmed && ui->cell_width && ui->cell_height) snprintf(title,sizeof title,"Image display: %s (%ux%u)",image_status_label(ui),ui->cell_width,ui->cell_height);
            page(win,title,detail,"Enter: query  M: manual  Esc: back");
            snprintf(detail,sizeof detail,"pdftoppm: %s | pdftotext: %s",tools.pdf_image?"found":"missing",tools.pdf_text?"found":"missing");
            draw_window_text(win,3,2,w-4,detail);
            draw_window_text(win,h-3,2,w-4,hint); dialog_refresh(win);
        } else if(stage==2) {
            snprintf(detail,sizeof detail,"Measured cell: %ux%u; rectangle below",candidate.width,candidate.height);
            page(win,"Is red/blue image inside the marked area?",detail,"y: display OK  n/Esc: cancel");
            int rows=h-6;
            if((24u+candidate.width-1)/candidate.width>(unsigned)(w-4) ||
               (12u+candidate.height-1)/candidate.height>(unsigned)rows) {
                snprintf(result,capacity,"Resize terminal for test image; settings kept"); break;
            }
            unsigned columns=(24u+candidate.width-1)/candidate.width;
            unsigned image_rows=(12u+candidate.height-1)/candidate.height;
            for(unsigned row=0;row<image_rows;row++) {
                mvwhline(win,3+(int)row,2,' ',(int)columns);
                mvwaddch(win,3+(int)row,1,'|');
                mvwaddch(win,3+(int)row,2+(int)columns,'|');
            }
            draw_window_text(win,h-3,2,w-4,"Image must stay between | markers"); dialog_refresh(win);
            /* Fixed 24x12 red/blue rectangle. No external converter or paths. */
            const char sample[]="\033P0;0;0q\"1;1;24;12#0;2;100;0;0!24~-#1;2;0;0;100!24~\033\\";
            if(!graphics_validate(sample,sizeof sample-1,24,12)) break;
            int y,x; getbegyx(win,y,x);
            fprintf(stdout,"\0337\033[?80s\033[?80l\033[%d;%dH",y+4,x+3);
            fwrite(sample,1,sizeof sample-1,stdout);
            fputs("\033[?80r\0338",stdout); fflush(stdout);
            ui->graphics_visible=true; showing=true; stage=3;
        } else if(stage==4) page(win,"Did the image disappear completely?","Check the former image area for any pixels.","y: erase OK  n/Esc: cancel");
        else if(stage==5) page(win,"Enable image preview for this session?","Auto/Off defaults are saved separately in F7.","y: enable  n/Esc: keep previous");
        wtimeout(win,50);
        int key=mouse_key(win,input_key(win));
        core_media_reap();
        if(key==ERR) { if(core_media_shutdown_requested()) break; continue; }
        if(key==KEY_RESIZE) { resized=true; snprintf(result,capacity,"Resized: image check stopped; retry in F7"); break; }
        if(key==27||key=='n') break;
        if(stage==0 && (key=='\n'||key==KEY_ENTER||key=='m'||key=='M')) {
            bool manual=key=='m'||key=='M';
            media_reset(ui); graphics_clear(ui);
            ui->image_status=IMAGE_CHECKING;
            page(win,"Checking: retiring current converter","No file operations run during this check.","Esc: cancel");
            uint64_t end=core_monotonic_ms()+1000;
            while(core_media_cleanup_pending() && core_monotonic_ms()<end) {
                core_media_reap(); int k=mouse_key(win,input_key(win));
                if(k==27||k==KEY_RESIZE) { resized=k==KEY_RESIZE; finished=true; break; }
            }
            if(finished) break;
            if(core_media_cleanup_pending()) { snprintf(result,capacity,"Converter cleanup pending; retry in F7"); break; }
            input_terminal_begin(&candidate);
            if(!manual) {
                page(win,"Checking terminal (up to 700 ms)","Requesting Sixel support and cell pixels.","Esc: cancel");
                Result r=terminal_query();
                if(r.code!=RESULT_OK) { snprintf(hint,sizeof hint,"%.159s",r.detail); input_terminal_end(); ui->image_status=previous_status; continue; }
                end=core_monotonic_ms()+700;
                while(core_monotonic_ms()<end) {
                    int k=mouse_key(win,input_key(win));
                    if(k==27||k==KEY_RESIZE) { resized=k==KEY_RESIZE; finished=true; break; }
                    /* Other keys belong to this modal and are never replayed as file commands. */
                }
            }
            input_terminal_end();
            if(finished) break;
            if(!candidate.cells_received) {
                unsigned a=0,b=0;
                if(core_terminal_pixels(&a,&b)&&a&&a<=64&&b&&b<=128) {
                    candidate.width=a; candidate.height=b; candidate.cells_received=true;
                }
            }
            if(manual) {
                char measured[UI_INPUT_CAP]; bool changed=false;
                if(!prompt_value_status(ui,"Measured WIDTHxHEIGHT (do not guess)",measured,sizeof measured,"",&changed)) {
                    resized=changed; break;
                }
                if(!dimensions(measured,&candidate.width,&candidate.height)) {
                    ui->image_status=IMAGE_NO_CELLS;
                    snprintf(hint,sizeof hint,"Invalid measured size; width 1-64, height 1-128"); continue;
                }
                candidate.cells_received=true;
            } else if(!candidate.da_received||!candidate.sixel) {
                ui->image_status=IMAGE_NO_RESPONSE;
                snprintf(hint,sizeof hint,"Auto check inconclusive; M: measured visual test"); continue;
            } else if(!candidate.cells_received) {
                ui->image_status=IMAGE_NO_CELLS;
                snprintf(hint,sizeof hint,"Cell size unknown; M: enter measured dimensions"); continue;
            }
            stage=2; continue;
        }
        if(stage==3 && key=='y') {
            graphics_clear(ui); showing=false; touchwin(stdscr); draw_cached(ui);
            stage=4; continue;
        }
        if(stage==4 && key=='y') { stage=5; continue; }
        if(stage==5 && key=='y') {
            ui->sixel_confirmed=true; ui->cell_width=candidate.width; ui->cell_height=candidate.height;
            ui->image_auto=true; ui->image_status=IMAGE_ENABLED;
            ui->image_probe=IMAGE_PROBE_DONE; ui->image_cells_fixed=true; ui->image_cells_stale=false; media_reset(ui);
            snprintf(result,capacity,"Image display enabled for this session"); finished=true; break;
        }
    }
    input_terminal_end();
    if(showing) graphics_clear(ui);
    if(ui->image_status==IMAGE_CHECKING) ui->image_status=previous_status;
    if(resized) snprintf(result,capacity,"Resized: image check stopped; retry in F7");
    dialog_close(ui,win);
    return resized;
}
