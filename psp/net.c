#include "net.h"
#include <pspkernel.h>
#include <pspctrl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

volatile int pt_stop = 0;
char pt_host[128];
int pt_port = 8080;
static int ready(int sock, int writing) {
    for (int ticks = 0; ticks < 48 && !pt_stop; ticks++) {
        fd_set set; FD_ZERO(&set); FD_SET(sock, &set);
        struct timeval timeout = {0, 250000};
        int result = select(sock + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &timeout);
        if (result != 0) return result > 0 ? 0 : -1;
    }
    return -1;
}
static int raw_read(int sock, void *buffer, unsigned int size) {
    unsigned int used = 0;
    while (used < size && !pt_stop) {
        if (ready(sock, 0) < 0) return -1;
        int n = recv(sock, (char *)buffer + used, size - used, 0);
        if (n <= 0) return -1;
        used += n;
    }
    return used == size ? 0 : -1;
}
void pt_close(PtStream *s) {if (s->socket >= 0) close(s->socket); s->socket = -1;}
int pt_read(PtStream *s, void *buffer, unsigned int size) {
    if (size > s->remaining || raw_read(s->socket, buffer, size) < 0) return -1;
    s->remaining -= size; return 0;
}
int pt_open(PtStream *s, const char *method, const char *path) {
    struct sockaddr_in address;
    char request[768], header[4096];
    s->socket = -1; s->remaining = 0; s->status = 0;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons(pt_port);
    if (inet_pton(AF_INET, pt_host, &address.sin_addr) != 1) {
        struct hostent *host = gethostbyname(pt_host);
        if (!host || host->h_length != 4) return -1;
        memcpy(&address.sin_addr, host->h_addr_list[0], 4);
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    s->socket = sock;
    int nonblocking = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_NONBLOCK, &nonblocking, sizeof(nonblocking)) < 0) goto fail;
    if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        int error = 0; socklen_t length = sizeof(error);
        if (ready(sock, 1) < 0 || getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) goto fail;
    }
    nonblocking = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_NONBLOCK, &nonblocking, sizeof(nonblocking)) < 0) goto fail;
    int length = snprintf(request, sizeof(request), "%s %s HTTP/1.0\r\nHost: %s:%d\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", method, path, pt_host, pt_port);
    if (length < 0 || length >= (int)sizeof(request)) goto fail;
    for (int sent = 0; sent < length;) {
        if (ready(sock, 1) < 0) goto fail;
        int n = send(sock, request + sent, length - sent, 0);
        if (n <= 0) goto fail;
        sent += n;
    }
    size_t used = 0;
    while (used < sizeof(header) - 1) {
        if (raw_read(sock, header + used, 1) < 0) goto fail;
        used++; header[used] = 0;
        if (used >= 4 && !memcmp(header + used - 4, "\r\n\r\n", 4)) break;
    }
    if (!strstr(header, "\r\n\r\n") || sscanf(header, "HTTP/%*d.%*d %d", &s->status) != 1) goto fail;
    int found = 0;
    for (char *line = strstr(header, "\r\n"); line && line[2]; line = strstr(line + 2, "\r\n")) {
        if (!strncasecmp(line + 2, "Transfer-Encoding:", 18) || !strncasecmp(line + 2, "Content-Encoding:", 17)) goto fail;
        if (!strncasecmp(line + 2, "Content-Length:", 15)) {
            char *end;
            unsigned long value = strtoul(line + 17, &end, 10);
            if (found || end == line + 17 || *end != '\r' || value > 256UL*1024*1024) goto fail;
            s->remaining = value; found = 1;
        }
    }
    if (!found) goto fail;
    return 0;
fail:
    pt_close(s); return -1;
}
int pt_text(const char *method, const char *path, char *buffer, size_t capacity) {
    PtStream s;
    if (pt_open(&s, method, path) < 0) {snprintf(buffer, capacity, "Connection failed. Check server address and Wi-Fi."); return -1;}
    unsigned int length = s.remaining;
    if (length >= capacity || pt_read(&s, buffer, length) < 0) {
        pt_close(&s); snprintf(buffer, capacity, "Invalid or incomplete server response."); return -1;
    }
    buffer[length] = 0;
    pt_close(&s);
    return s.status == 200 ? 0 : -1;
}
