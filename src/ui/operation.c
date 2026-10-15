#include "ui.h"

/* UI-only progress/input handler; background commands are never dispatched.
   Drawing is throttled; input polling is not. */
typedef struct {
    WINDOW *win;
    MutationJob *job;
    bool copy, trash, move, drawn, cancelled, asynchronous, cancel_drawn, uncertain;
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
        dialog_frame(view->win, view->batch ? (view->action==BATCH_TRASH ? "Moving to Trash" : view->action==BATCH_MOVE ? "Batch moving" : view->action==BATCH_COPY ? "Batch copying" : "Batch deleting") : view->trash ? "Moving to Trash" : view->move ? "Moving" : view->copy ? "Copying" : "Deleting");
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
            else snprintf(text, sizeof text, "%s",view->trash ? "No permanent-delete fallback" : view->move?"Atomic move; no cross-filesystem fallback":"Deleted items cannot be restored");
            draw_window_text(view->win, 3, 2, w - 4, text);
            draw_window_text(view->win, 4, 2, w - 4, "Cancel stops here; changes are kept");
        }
        dialog_button(view->win, h - 2, 2, "[ Cancel ]", true, true);
        draw_window_text(view->win, h - 2, 14, w - 16, view->asynchronous?"Esc / Enter; F10: exit":"Esc / Enter");
        dialog_refresh(view->win); view->last_draw = now; view->drawn = true;
    }
    if(core_media_shutdown_requested() || core_editor_shutdown_requested())view->cancelled=true;
    /* Bound input work even if a terminal continuously sends unrelated events. */
    for (int i = 0; i < 64 && (!view->cancelled || view->asynchronous); i++) {
        wint_t key; int kind = input_wide(view->win, &key);
        if (kind == ERR) break;
        if(view->asynchronous && kind==KEY_CODE_YES && key==KEY_F(10)) { core_request_shutdown();view->cancelled=true; }
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
    /* Queue the request before publishing its acknowledgement. The worker may
       reach its next safe point as soon as that frame is visible. */
    if(view->asynchronous && view->cancelled && view->job)core_mutation_cancel(view->job);
    if(view->asynchronous && view->cancelled && (!view->cancel_drawn || now-view->last_draw>=100)) {
        int h,w;getmaxyx(view->win,h,w);
        if(view->win==stdscr)erase();else dialog_frame(view->win,view->uncertain?"Result uncertain":"Cancellation requested");
        int row=view->win==stdscr?0:1;
        draw_window_text(view->win,row,view->win==stdscr?0:2,w-(view->win==stdscr?0:4),view->uncertain?"Result uncertain; waiting for worker cleanup":"Cancellation requested; waiting for worker cleanup");
        if(h>row+2)draw_window_text(view->win,row+1,2,w-4,"Current system call may still be running. Changes are kept.");
        dialog_refresh(view->win);view->cancel_drawn=true;view->last_draw=now;
    }
    return !view->cancelled;
}
typedef struct { MutationJob *job; BatchJob *batch; bool *done; } ConflictWait;
static bool conflict_pending(void *context) {
    ConflictWait *wait=context;
    (void)core_mutation_poll(wait->job,wait->done);
    return !*wait->done && wait->batch->result.code!=RESULT_UNCERTAIN &&
        !core_media_shutdown_requested() && !core_editor_shutdown_requested();
}
static void run_async(UiContext *ui,BatchJob *batch,OperationView *view) {
    view->asynchronous=true;
    OperationProgress initial={.path=batch->targets[0].source};
    (void)operation_progress(&initial,view);
    if(view->cancelled) { batch_cancel(batch);return; }
    MutationJob *job=NULL;Result r=core_mutation_start(batch,&job);
    if(r.code!=RESULT_OK) { batch->result=r;return; }
    view->job=job;
    bool done=false;
    while(!done) {
        (void)core_mutation_poll(job,&done);
        if(batch->result.code==RESULT_UNCERTAIN) { view->uncertain=true;view->cancelled=true; }
        if(LINES!=view->height || COLS!=view->width) {
            view->cancelled=true;view->height=LINES;view->width=COLS;view->cancel_drawn=false;
            if(view->win!=stdscr)dialog_close(ui,view->win);
            view->win=LINES>=9 && COLS>=50?dialog_open(ui,"Cancelling operation",7,76):stdscr;
            if(!view->win)view->win=stdscr;
            wtimeout(view->win,0);
        }
        const MutationProgress *p=core_mutation_progress(job);
        view->index=p->index;view->total=p->total;view->succeeded=p->succeeded;view->skipped=p->skipped;
        OperationProgress progress={p->path,p->completed_items,p->copied_bytes};
        (void)operation_progress(&progress,view);
        if(view->cancelled && !done)core_mutation_cancel(job);
        size_t index;
        if(!done && !view->cancelled && core_mutation_conflict(job,&index)) {
            char title[160];snprintf(title,sizeof title,"Name collision: %.120s",batch->targets[index].name);
            const char *choices[]={"Stop batch (no overwrite)","Skip this target","Skip all later name collisions"};
            ConflictWait wait={job,batch,&done};
            int selected=ui_choices_poll(ui,title,choices,3,conflict_pending,&wait);
            if(!done)core_mutation_decide(job,selected<0?CONFLICT_CANCEL:(BatchConflictDecision)selected);
            view->drawn=false;touchwin(view->win);
        }
        if(!done)napms(10);
    }
    core_mutation_close(job);view->job=NULL;
}
static Result run_single(UiContext *ui,BatchAction action,const char *source,const char *directory,const char *name,char **destination,OperationView *view) {
    BatchJob batch={.action=action};
    batch.targets=calloc(1,sizeof *batch.targets);
    if(!batch.targets)return result_make(RESULT_NO_MEMORY,"Out of memory; no changes");
    batch.len=1;batch.targets[0].source=text_copy(source);batch.targets[0].name=name?text_copy(name):core_path_name(source);
    batch.directory=directory?text_copy(directory):NULL;
    if(!batch.targets[0].source || !batch.targets[0].name || (directory&&!batch.directory)) {
        batch_free(&batch);return result_make(RESULT_NO_MEMORY,"Out of memory; no changes");
    }
    run_async(ui,&batch,view);Result r=batch.result;
    if(destination) { *destination=batch.targets[0].destination;batch.targets[0].destination=NULL; }
    batch_free(&batch);return r;
}
static void operation_close(UiContext *ui,OperationView *view) {
    flushinp();if(view->win!=stdscr)dialog_close(ui,view->win);else draw(ui);
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
    Result r;
    if(view.win && ui->async_reads)r=run_single(ui,copy?BATCH_COPY:BATCH_DELETE,source,directory,name,destination,&view);
    else r = copy ? core_transfer_progress(false, source, directory, name, destination,
                                             view.win ? operation_progress : NULL, &view) :
                      core_delete_progress(source, view.win ? operation_progress : NULL, &view);
    if (view.win) {
        /* Discard queued keyboard/mouse commands, including those behind Cancel.
           Resize has already been consumed, and draw() uses the new dimensions. */
        operation_close(ui,&view);
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
    if(view.win && ui->async_reads)run_async(ui,job,&view);
    else batch_execute(job,view.win?batch_progress:NULL,&view);
    job->conflict=NULL;job->conflict_context=NULL;
    if(view.win)operation_close(ui,&view);
}

Result run_trash_operation(UiContext *ui,const char *source,char **destination) {
    OperationView view={.trash=true,.height=LINES,.width=COLS};
    if(stdscr) {
        view.win=dialog_open(ui,"Moving to Trash",7,76);
        if(!view.win) return result_make(RESULT_CANCELLED,"Cannot open progress window; no changes");
        wtimeout(view.win,0);
    }
    Result r=view.win && ui->async_reads?run_single(ui,BATCH_TRASH,source,NULL,NULL,destination,&view):core_trash_progress(source,destination,view.win?operation_progress:NULL,&view);
    if(view.win)operation_close(ui,&view);
    return r;
}

Result run_move_operation(UiContext *ui,const char *source,const char *directory,const char *name,char **destination) {
    if(!stdscr || !ui->async_reads)return core_transfer(true,source,directory,name,destination);
    OperationView view={.move=true,.height=LINES,.width=COLS};
    view.win=dialog_open(ui,"Moving",7,76);
    if(!view.win)return result_make(RESULT_CANCELLED,"Cannot open progress window; no changes");
    wtimeout(view.win,0);
    Result r=run_single(ui,BATCH_MOVE,source,directory,name,destination,&view);
    operation_close(ui,&view);return r;
}
