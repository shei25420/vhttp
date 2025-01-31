/*
 * Copyright (c) 2014 DeNA Co., Ltd.
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
#ifndef vhttp__http1_h
#define vhttp__http1_h

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vhttp_http1_upgrade_cb)(void *user_data, vhttp_socket_t *sock, size_t reqsize);

void vhttp_http1_accept(vhttp_accept_ctx_t *ctx, vhttp_socket_t *sock, struct timeval connected_at);
void vhttp_http1_upgrade(vhttp_req_t *req, vhttp_iovec_t *inbufs, size_t inbufcnt, vhttp_http1_upgrade_cb on_complete, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
