#include "ui.h"

static bool current_cells(unsigned *width,unsigned *height) {
    return core_terminal_pixels(width,height) && *width && *width<=64 && *height && *height<=128;
}
static void finish(UiContext *ui,int status) {
    input_terminal_end();
    ui->image_probe=IMAGE_PROBE_DONE;
    ui->image_status=status;
}
void graphics_probe_cancel(UiContext *ui) {
    if(ui->image_probe==IMAGE_PROBE_WAITING)
        finish(ui,ui->sixel_confirmed ? IMAGE_NO_CELLS : IMAGE_CANCELLED);
}
static void begin(UiContext *ui,bool cells_only) {
    ui->image_probe=IMAGE_PROBE_WAITING;
    ui->image_probe_cells_only=cells_only;
    ui->image_cells_stale=false;
    ui->image_status=IMAGE_CHECKING;
    if(!cells_only) ui->cell_width=ui->cell_height=0;
    ui->image_deadline=core_monotonic_ms()+700;
    /* The capture is context-owned, never a pointer into a returned stack frame. */
    input_terminal_begin_timed(&ui->image_reply,ui->image_deadline);
    unsigned width=0,height=0;
    if(current_cells(&width,&height)) {
        ui->image_reply.cells_received=true;
        ui->image_reply.width=width; ui->image_reply.height=height;
    }
    Result r=cells_only ? terminal_query_cells() : terminal_query();
    if(r.code!=RESULT_OK) finish(ui,IMAGE_QUERY_FAILED);
}
/* File selection cannot invalidate a terminal-wide result. A modal, Off,
   hidden preview or shutdown instead detaches capture before owning input. */
void graphics_probe_poll(UiContext *ui) {
    if(!ui->image_auto || ui->modal_depth || !ui_preview_enabled(ui) || core_media_shutdown_requested()) {
        graphics_probe_cancel(ui); return;
    }
    int h=0,w=0;
    if(stdscr) getmaxyx(stdscr,h,w);
    bool resized=ui->image_columns && (w!=ui->image_columns || h!=ui->image_rows);
    ui->image_columns=w; ui->image_rows=h;
    if(resized) {
        graphics_probe_cancel(ui);
        if(ui->sixel_confirmed && !ui->image_cells_fixed) {
            ui->cell_width=ui->cell_height=0;
            unsigned width=0,height=0;
            if(current_cells(&width,&height)) {
                ui->cell_width=width; ui->cell_height=height;
                ui->image_cells_stale=false; ui->image_status=IMAGE_ENABLED;
            } else ui->image_cells_stale=true; /* Refresh lazily, only if media needs it. */
        }
    }
    if(h<9 || w<50 || w-ui_layout(w,h,true).list_width-4<12 || h-7-PREVIEW_METADATA_ROWS-1<3) {
        graphics_probe_cancel(ui); return;
    }
    if(ui->image_probe!=IMAGE_PROBE_WAITING) return;
    TerminalReply *reply=&ui->image_reply;
    if(!ui->image_probe_cells_only && reply->da_received && !reply->sixel) {
        finish(ui,IMAGE_UNSUPPORTED); return;
    }
    if(reply->da_received && reply->sixel) ui->sixel_confirmed=true;
    bool support=ui->image_probe_cells_only || (reply->da_received && reply->sixel);
    if(support && reply->cells_received) {
        ui->sixel_confirmed=true;
        ui->cell_width=reply->width; ui->cell_height=reply->height;
        ui->image_cells_fixed=false;
        finish(ui,IMAGE_ENABLED); return;
    }
    if(core_monotonic_ms()>=ui->image_deadline)
        finish(ui,support ? IMAGE_NO_CELLS : IMAGE_NO_RESPONSE);
}
void graphics_probe_prepare(UiContext *ui,int rows) {
    if(!ui->image_auto || ui->modal_depth || !ui_preview_enabled(ui) || core_media_shutdown_requested()) return;
    int h=0,w=0;
    if(stdscr) getmaxyx(stdscr,h,w);
    UiLayout layout=ui_layout(w,h,true);
    if(h<9 || w<50 || w-layout.list_width-4<12 || rows-PREVIEW_METADATA_ROWS-1<3) return;
    if(ui->sixel_confirmed && ui->image_cells_stale) begin(ui,true);
    else if(!ui->sixel_confirmed && ui->image_probe==IMAGE_PROBE_IDLE) begin(ui,false);
}
