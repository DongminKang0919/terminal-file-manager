#ifndef TFILE_TERMINAL_H
#define TFILE_TERMINAL_H
#include "../model.h"
/* Isolated terminal protocol, unrelated to file-operation policy. */
typedef struct { bool da_received, sixel, cells_received; unsigned width,height; } TerminalReply;
bool terminal_report(const char *sequence,size_t length,TerminalReply *reply);
Result terminal_session_check(void);
Result terminal_query(void);
Result terminal_query_cells(void);
TerminalTools terminal_tools(void);
#endif
