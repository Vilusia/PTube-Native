#ifndef PTUBE_NET_H
#define PTUBE_NET_H
#include <stddef.h>
typedef struct {int socket; unsigned int remaining; int status;} PtStream;
extern volatile int pt_stop;
extern char pt_host[128];
extern int pt_port;
int pt_open(PtStream *stream, const char *method, const char *path);
int pt_read(PtStream *stream, void *buffer, unsigned int size);
void pt_close(PtStream *stream);
int pt_text(const char *method, const char *path, char *buffer, size_t capacity);
#endif
