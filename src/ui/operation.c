#include "ui.h"

/* The synchronous callback is the only input handler while a job is active.
   It never dispatches commands. Drawing is throttled; polling is not. */
typedef struct {
    WINDOW *win;
    bool copy, trash, drawn, cancelled;
    int height, width;
    uint64_t last_draw;
    bool batch; BatchAction action; size_t index, total, succeeded, skipped;
} OperationView;
static bool operation_progress(const OperationProgress *progress, void *context) {
    OperationView *view = context;
    if (LINES != view->height || COLS != view->width) view->cancelled = true;
    uint64_t now = core_monotonic_ms();
    if (!view->cancelled && (!view->drawn || now - view->last_draw >= 100)) {
        int h, w; getmaxyx(view->win, h, w);
        dialog_frame(view->win, view->batch ? (view->action==BATCH_TRASH ? "Moving to Trash" : view->action==BATCH_MOVE ? "Batch moving" : view->action==BATCH_COPY ? "Batch copying" : "Batch deleting") : view->trash ? "Moving to Trash" : view->copy ? "Copying" : "Deleting");
        if (view->batch) {
            char counts[120];
            snprintf(counts,sizeof counts,"Target %zu/%zu | Succeeded: %zu | Skipped: %zu",view->index+1,view->total,view->succeeded,view->skipped);
            draw_window_text(view->win,1,2,w-4,counts);
            draw_window_text(view->win,2,2,w-4,progress->path);
            snprintf(counts,sizeof counts,"Recursive completed items: %llu",(unsigned long long)progress->completed_items);
            draw_window_text(view->win,3,2,w-4,counts);
            snprintf(counts,sizeof counts,"Copied bytes: %llu",(unsigned long long)progress->copied_bytes);
            draw_window_text(view->win,4,2,w-4,counts);
        } else {
            draw_window_text(view->win, 1, 2, w - 4, progress->path);
            char text[96];
            snprintf(text, sizeof text, "Completed items: %llu", (unsigned long long)progress->completed_items);
            draw_window_text(view->win, 2, 2, w - 4, text);
            if (view->copy) snprintf(text, sizeof text, "Copied bytes: %llu", (unsigned long long)progress->copied_bytes);
            else snprintf(text, sizeof text, "%s",view->trash ? "No permanent-delete fallback" : "Deleted items cannot be restored");
            draw_window_text(view->win, 3, 2, w - 4, text);
            draw_window_text(view->win, 4, 2, w - 4, "Cancel stops here; changes are kept");
        }
        dialog_button(view->win, h - 2, 2, "[ Cancel ]", true, true);
        draw_window_text(view->win, h - 2, 14, w - 16, "Esc / Enter");
        dialog_refresh(view->win); view->last_draw = now; view->drawn = true;
    }
    /* Bound input work even if a terminal continuously sends unrelated events. */
    for (int i = 0; i < 64 && !view->cancelled; i++) {
        wint_t key; int kind = input_wide(view->win, &key);
        if (kind == ERR) break;
        if ((kind == OK && (key == 27 || key == '\n' || key == '\r')) ||
            (kind == KEY_CODE_YES && (key == KEY_RESIZE || key == KEY_ENTER))) view->cancelled = true;
        if (kind == KEY_CODE_YES && key == KEY_MOUSE) {
            MEVENT e;
            if (getmouse(&e) == OK && mouse_click(&e)) {
                int h=getmaxy(view->win);
                if (dialog_closed(view->win, &e) ||
                    dialog_button_hit(view->win,&e,h-2,2,"[ Cancel ]",true)) view->cancelled = true;
            }
        }
    }
    return !view->cancelled;
}
Result run_file_operation(UiContext *ui, bool copy, const char *source,
                          const char *directory, const char *name, char **destination) {
    OperationView view = {.copy = copy, .height = LINES, .width = COLS};
    /* Controllers also run without a terminal in native regression tests. */
    if (stdscr) {
        view.win = dialog_open(ui, copy ? "Copying" : "Deleting", 7, 76);
        if (!view.win) return result_make(RESULT_CANCELLED, "Cannot open progress window; no changes");
        wtimeout(view.win, 0);
    }
    Result r = copy ? core_transfer_progress(false, source, directory, name, destination,
                                             view.win ? operation_progress : NULL, &view) :
                      core_delete_progress(source, view.win ? operation_progress : NULL, &view);
    if (view.win) {
        /* Discard queued keyboard/mouse commands, including those behind Cancel.
           Resize has already been consumed, and draw() uses the new dimensions. */
        flushinp();
        dialog_close(ui, view.win);
    }
    return r;
}

static bool batch_progress(const BatchProgress *p,void *context) {
    OperationView *view=context; view->index=p->index; view->total=p->total; view->succeeded=p->succeeded; view->skipped=p->skipped;
    return operation_progress(&p->progress,view);
}
typedef struct { UiContext *ui; OperationView *view; } CollisionView;
static BatchConflictDecision batch_conflict(const BatchTarget *target,const char *directory,void *context) {
    CollisionView *cv=context; OperationView *view=cv->view;
    if(LINES!=view->height || COLS!=view->width || core_media_shutdown_requested()) return CONFLICT_CANCEL;
    char title[160];snprintf(title,sizeof title,"Name collision: %.120s",target->name);
    const char *choices[]={"Stop batch (no overwrite)","Skip this target","Skip all later name collisions"};
    /* The frozen source/destination are not modified by this modal. */
    (void)directory;
    int selected=ui_choices(cv->ui,title,choices,3);
    view->drawn=false; if(view->win) touchwin(view->win);
    if(selected<0 || LINES!=view->height || COLS!=view->width || core_media_shutdown_requested()) return CONFLICT_CANCEL;
    return selected==1?CONFLICT_SKIP:selected==2?CONFLICT_SKIP_ALL:CONFLICT_STOP;
}
void run_batch_operation(UiContext *ui,BatchJob *job) {
    OperationView view={.batch=true,.action=job->action,.height=LINES,.width=COLS};
    if(stdscr) {
        view.win=dialog_open(ui,"Batch operation",7,76);
        if(!view.win) { batch_cancel(job);return; }
        wtimeout(view.win,0);
    }
    CollisionView collision={ui,&view};
    if(view.win && (job->action==BATCH_COPY || job->action==BATCH_MOVE)) { job->conflict=batch_conflict;job->conflict_context=&collision; }
    batch_execute(job,view.win?batch_progress:NULL,&view);
    job->conflict=NULL;job->conflict_context=NULL;
    if(view.win) { flushinp();dialog_close(ui,view.win); }
}

Result run_trash_operation(UiContext *ui,const char *source,char **destination) {
    OperationView view={.trash=true,.height=LINES,.width=COLS};
    if(stdscr) {
        view.win=dialog_open(ui,"Moving to Trash",7,76);
        if(!view.win) return result_make(RESULT_CANCELLED,"Cannot open progress window; no changes");
        wtimeout(view.win,0);
    }
    Result r=core_trash_progress(source,destination,view.win?operation_progress:NULL,&view);
    if(view.win) { flushinp();dialog_close(ui,view.win); }
    return r;
}
