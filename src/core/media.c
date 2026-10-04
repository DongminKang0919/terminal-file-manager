#include "core.h"
#include "../platform/platform.h"
#include <stdlib.h>
struct MediaPreview { PlatformMedia *job; };
Result core_media_open(const char *path,PreviewMediaKind kind,bool text,unsigned width,unsigned height,MediaPreview **out) {
    *out=NULL;
    MediaPreview *m=calloc(1,sizeof *m);
    if(!m) return result_make(RESULT_NO_MEMORY,"Out of memory");
    Result r=platform_media_open(path,kind,text,width,height,&m->job);
    if(r.code!=RESULT_OK) { free(m); return r; }
    *out=m; return r;
}
Result core_media_poll(MediaPreview *m,bool *done,char **data,size_t *len) { return platform_media_poll(m->job,done,data,len); }
void core_media_close(MediaPreview *m) { if(m) { platform_media_close(m->job); free(m); } }
void core_media_reap(void) { platform_media_reap(); }
void core_media_shutdown(void) { platform_media_shutdown(); }
bool core_terminal_pixels(unsigned *width,unsigned *height) { return platform_terminal_pixels(width,height); }

bool core_media_cleanup_pending(void) { return platform_media_cleanup_pending(); }

void core_media_install_signals(void) { platform_media_install_signals(); }
bool core_media_shutdown_requested(void) { return platform_media_shutdown_requested(); }
