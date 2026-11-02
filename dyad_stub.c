#include <stdint.h>
#include <string.h>

#include "dyad.h"

#define STREAMS 16
#define OUT_SIZE 65536

struct dyad_Stream {
    int port;
    dyad_Callback onAccept;
    dyad_Callback onData;
    void *udata;
    dyad_Stream *remote;
};

static dyad_Stream streams[STREAMS];
static int streamCount;
static dyad_Stream *byPort[STREAMS];
static uint8_t out[OUT_SIZE];
static uint32_t outLen;
static uint8_t in[OUT_SIZE];

dyad_Stream *dyad_newStream(void)
{
    return &streams[streamCount++];
}

void dyad_setNoDelay(dyad_Stream *stream, int opt) { (void)stream; (void)opt; }
void dyad_setTimeout(dyad_Stream *stream, double seconds) { (void)stream; (void)seconds; }
void dyad_close(dyad_Stream *stream) { (void)stream; }

void dyad_addListener(dyad_Stream *stream, int event, dyad_Callback callback, void *udata)
{
    if (event == DYAD_EVENT_ACCEPT) {
        stream->onAccept = callback;
    } else if (event == DYAD_EVENT_DATA) {
        stream->onData = callback;
    }
    stream->udata = udata;
}

int dyad_listenEx(dyad_Stream *stream, const char *host, int port, int backlog)
{
    (void)host;
    (void)backlog;
    stream->port = port - 5761;
    stream->remote = dyad_newStream();
    stream->remote->port = stream->port;
    byPort[stream->port] = stream->remote;
    dyad_Event e = { .type = DYAD_EVENT_ACCEPT, .udata = stream->udata, .stream = stream, .remote = stream->remote };
    stream->onAccept(&e);
    return 0;
}

void dyad_write(dyad_Stream *stream, const void *data, int size)
{
    (void)stream;
    if (outLen + size > OUT_SIZE) {
        size = OUT_SIZE - outLen;
    }
    memcpy(&out[outLen], data, size);
    outLen += size;
}

__attribute__((export_name("bf_in"))) uint8_t *bfIn(void)
{
    return in;
}

__attribute__((export_name("bf_serial"))) int bfSerial(int port, int size)
{
    dyad_Stream *s = byPort[port];
    if (!s || !s->onData) {
        return 0;
    }
    dyad_Event e = { .type = DYAD_EVENT_DATA, .udata = s->udata, .stream = s, .data = (char *)in, .size = size };
    s->onData(&e);
    return 1;
}

__attribute__((export_name("bf_out"))) uint8_t *bfOut(void)
{
    return out;
}

__attribute__((export_name("bf_out_len"))) uint32_t bfOutLen(void)
{
    uint32_t len = outLen;
    outLen = 0;
    return len;
}
