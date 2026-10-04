#define _XOPEN_SOURCE 700
#include "../src/ui/ui.h"
#include <assert.h>

static uint64_t tick;
static unsigned queries,cell_queries,pixel_width,pixel_height;
static ResultCode query_result=RESULT_OK;
uint64_t __wrap_core_monotonic_ms(void) { return tick; }
bool __wrap_core_terminal_pixels(unsigned *w,unsigned *h) { *w=pixel_width; *h=pixel_height; return true; }
Result __wrap_terminal_query(void) { queries++; return result_make(query_result,NULL); }
Result __wrap_terminal_query_cells(void) { cell_queries++; return result_make(query_result,NULL); }
static UiContext context(void) {
    UiContext ui={.image_auto=true,.mode=UI_LIST_PREVIEW};
    graphics_init(&ui); return ui;
}
static void start(UiContext *ui) { graphics_probe_poll(ui); graphics_probe_prepare(ui,17); }
int main(void) {
    unsetenv("TFILE_SIXEL"); unsetenv("TFILE_CELL_PIXELS");
    FILE *out=tmpfile(),*in=tmpfile(); assert(out&&in);
    SCREEN *screen=newterm("xterm",out,in); assert(screen);
    resizeterm(24,100); input_init();
    UiContext ui=context();
    assert(!queries); ui.image_auto=false; start(&ui); assert(!queries);
    ui.image_auto=true; start(&ui); assert(queries==1&&media_pending(&ui));
    ui.image_reply=(TerminalReply){.da_received=true,.sixel=true,.cells_received=true,.width=8,.height=16};
    graphics_probe_poll(&ui); assert(ui.sixel_confirmed&&ui.image_status==IMAGE_ENABLED&&!media_pending(&ui));
    for(int n=0;n<100;n++) start(&ui);
    assert(queries==1&&!cell_queries);
    resizeterm(20,80); start(&ui); assert(queries==1&&cell_queries==1&&media_pending(&ui));
    ui.image_reply=(TerminalReply){.cells_received=true,.width=10,.height=20};
    graphics_probe_poll(&ui); assert(ui.cell_width==10&&ui.cell_height==20);
    pixel_width=9;pixel_height=18;resizeterm(24,100);start(&ui);
    assert(cell_queries==1&&ui.cell_width==9&&ui.cell_height==18);
    pixel_width=pixel_height=0;

    setenv("TFILE_SIXEL","1",1);setenv("TFILE_CELL_PIXELS","8x999",1);
    ui=context();assert(!ui.sixel_confirmed&&!ui.cell_width&&!ui.cell_height);
    start(&ui);ui.image_reply.da_received=ui.image_reply.sixel=true;
    tick+=701;graphics_probe_poll(&ui);assert(ui.image_status==IMAGE_NO_CELLS&&!ui.cell_width&&!ui.cell_height);
    setenv("TFILE_CELL_PIXELS","8x16",1);
    ui=context();unsigned explicit_queries=queries;start(&ui);
    assert(ui.sixel_confirmed&&ui.image_cells_fixed&&queries==explicit_queries);
    unsetenv("TFILE_SIXEL");unsetenv("TFILE_CELL_PIXELS");

    ui=context();start(&ui);tick+=701;graphics_probe_poll(&ui);
    assert(ui.image_status==IMAGE_NO_RESPONSE&&!media_pending(&ui));
    unsigned before=queries;start(&ui);assert(queries==before);
    ui=context();start(&ui);ui.image_reply.da_received=true;graphics_probe_poll(&ui);
    assert(ui.image_status==IMAGE_UNSUPPORTED&&!ui.sixel_confirmed);
    ui=context();start(&ui);ui.image_reply.da_received=ui.image_reply.sixel=true;
    tick+=701;graphics_probe_poll(&ui);assert(ui.image_status==IMAGE_NO_CELLS);
    assert(!ui.cell_width&&!ui.cell_height);
    ui=context();query_result=RESULT_IO;start(&ui);
    assert(ui.image_status==IMAGE_QUERY_FAILED&&!media_pending(&ui));query_result=RESULT_OK;
    ui=context();start(&ui);ui.modal_depth=1;graphics_probe_poll(&ui);
    assert(ui.image_status==IMAGE_CANCELLED&&!media_pending(&ui));
    ui=context();start(&ui);resizeterm(8,49);graphics_probe_poll(&ui);
    assert(!media_pending(&ui));resizeterm(24,100);before=queries;start(&ui);assert(queries==before);
    ui=context();start(&ui);ui_free(&ui);assert(!media_pending(&ui));
    endwin();delscreen(screen);fclose(out);fclose(in);
    puts("PASS: lazy bounded session probe, Off/failure/cancellation, geometry refresh and capture lifetime");
}
