#include "ui.h"

Result ui_init(UiContext *ui,const char *directory) {
    *ui=(UiContext){.show_preview=true,.wheel_step=1};
    return app_init(&ui_panel(ui)->app,directory);
}
void ui_free(UiContext *ui) {
    notice_clear(ui); preview_reset(ui);
    app_free(&ui_panel(ui)->app);
}
