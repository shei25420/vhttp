/*
 * Copyright (c) 2021 Fastly, Kazuho Oku
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <inttypes.h>
#include "vhttp.h"

#define SEND_WAIT 500        /* how frequent new loglines should be pushed out; in milliseconds */
#define BUFFER_LIMIT 8388608 /* do not buffer more than 8MB */

struct vhttp_self_trace_generator {
    vhttp_generator_t super;
    vhttp_req_t *req;
    vhttp_buffer_t *buf;
    vhttp_doublebuffer_t inflight;
    vhttp_timer_t send_timer;
    unsigned should_send_buffered : 1;
};

static void on_generator_dispose(void *_self)
{
    struct vhttp_self_trace_generator *self = _self;

    quicly_tracer_t *tracer = self->req->conn->callbacks->get_tracer(self->req->conn);
    tracer->cb = NULL;
    tracer->ctx = NULL;

    vhttp_buffer_dispose(&self->buf);
    vhttp_doublebuffer_dispose(&self->inflight);
    vhttp_timer_unlink(&self->send_timer);
}

static void do_send(struct vhttp_self_trace_generator *self)
{
    vhttp_iovec_t vec = vhttp_doublebuffer_prepare(&self->inflight, &self->buf, SIZE_MAX);
    vhttp_send(self->req, &vec, 1, vhttp_SEND_STATE_IN_PROGRESS);
}

static void on_send_timeout(vhttp_timer_t *_timer)
{
    struct vhttp_self_trace_generator *self = vhttp_STRUCT_FROM_MEMBER(struct vhttp_self_trace_generator, send_timer, _timer);

    assert(!self->inflight.inflight);
    assert(self->should_send_buffered);

    self->should_send_buffered = 0;
    do_send(self);
}

static void adjust_send_timer(struct vhttp_self_trace_generator *self)
{
    if (!self->inflight.inflight && self->should_send_buffered && !vhttp_timer_is_linked(&self->send_timer))
        vhttp_timer_link(self->req->conn->ctx->loop, SEND_WAIT, &self->send_timer);
}

static void log_trace(void *_self, const char *fmt, ...)
{
    struct vhttp_self_trace_generator *self = _self;

    /* append provided input to the buffer */
    if (self->buf->size < BUFFER_LIMIT) {
        va_list args;
        va_start(args, fmt);

        vhttp_iovec_t buf = vhttp_buffer_reserve(&self->buf, 1024);
        int len = vsnprintf(buf.base, buf.len, fmt, args);
        if (len >= buf.len) {
            buf = vhttp_buffer_reserve(&self->buf, len + 1);
            len = vsnprintf(buf.base, buf.len, fmt, args);
            assert(len < buf.len);
        }
        self->buf->size += len;

        va_end(args);
    }

    /* Log is sent only when there's another request inflight. Otherwise, it is buffered until another request becomes inflight. */
    if (!self->should_send_buffered && self->req->conn->callbacks->num_reqs_inflight(self->req->conn) > 1)
        self->should_send_buffered = 1;
    adjust_send_timer(self);
}

static void do_proceed(vhttp_generator_t *_self, vhttp_req_t *_req)
{
    struct vhttp_self_trace_generator *self = (void *)_self;

    assert(self->inflight.inflight);
    vhttp_doublebuffer_consume(&self->inflight);

    adjust_send_timer(self);
}

static int on_req(vhttp_handler_t *handler, vhttp_req_t *req)
{
    if (req->conn->callbacks->get_tracer == NULL) {
        vhttp_send_error_403(req, "Forbidden", "not available", 0);
        return 0;
    }

    quicly_tracer_t *tracer = req->conn->callbacks->get_tracer(req->conn);
    if (tracer->cb != NULL) {
        vhttp_send_error_403(req, "Forbidden", "conn-state handler is already attached", 0);
        return 0;
    }

    /* instantiate the generator */
    struct vhttp_self_trace_generator *self = vhttp_mem_alloc_shared(&req->pool, sizeof(*self), on_generator_dispose);
    self->super.proceed = do_proceed;
    self->super.stop = NULL;
    self->req = req;
    vhttp_buffer_init(&self->buf, &vhttp_socket_buffer_prototype);
    vhttp_doublebuffer_init(&self->inflight, &vhttp_socket_buffer_prototype);
    self->send_timer = (vhttp_timer_t){.cb = on_send_timeout};
    self->should_send_buffered = 0;

    /* register */
    tracer->cb = log_trace;
    tracer->ctx = self;

    /* build response headers */
    req->res.status = 200;
    vhttp_add_header(&req->pool, &req->res.headers, vhttp_TOKEN_CONTENT_TYPE, NULL, vhttp_STRLIT("text/plain; charset=utf-8"));
    vhttp_add_header(&req->pool, &req->res.headers, vhttp_TOKEN_CACHE_CONTROL, NULL, vhttp_STRLIT("no-cache, no-store"));
    vhttp_buffer_append(&self->buf, "\n", 1); /* add some data for simplicity */

    vhttp_start_response(self->req, &self->super);
    do_send(self);

    return 0;
}

void vhttp_self_trace_register(vhttp_pathconf_t *conf)
{
    vhttp_handler_t *self = vhttp_create_handler(conf, sizeof(*self));
    self->on_req = on_req;
}
