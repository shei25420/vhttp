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
#include "vhttp/http2_common.h"

const vhttp_http2_priority_t vhttp_http2_default_priority = {
    0, /* exclusive */
    0, /* dependency */
    16 /* weight */
};

const vhttp_http2_settings_t vhttp_HTTP2_SETTINGS_DEFAULT = {
    /* header_table_size */ 4096,
    /* enable_push */ 1,
    /* max_concurrent_streams */ UINT32_MAX,
    /* initial_window_size */ 65535,
    /* max_frame_size */ 16384};

int vhttp_http2_update_peer_settings(vhttp_http2_settings_t *settings, const uint8_t *src, size_t len, const char **err_desc)
{
    for (; len >= 6; len -= 6, src += 6) {
        uint16_t identifier = vhttp_http2_decode16u(src);
        uint32_t value = vhttp_http2_decode32u(src + 2);
        switch (identifier) {
#define SET(label, member, min, max, err_code)                                                                                     \
    case vhttp_HTTP2_SETTINGS_##label:                                                                                               \
        if (!(min <= value && value <= max)) {                                                                                     \
            *err_desc = "invalid SETTINGS frame";                                                                                  \
            return err_code;                                                                                                       \
        }                                                                                                                          \
        settings->member = value;                                                                                                  \
        break
            SET(HEADER_TABLE_SIZE, header_table_size, 0, UINT32_MAX, 0);
            SET(ENABLE_PUSH, enable_push, 0, 1, vhttp_HTTP2_ERROR_PROTOCOL);
            SET(MAX_CONCURRENT_STREAMS, max_concurrent_streams, 0, UINT32_MAX, 0);
            SET(INITIAL_WINDOW_SIZE, initial_window_size, 0, 0x7fffffff, vhttp_HTTP2_ERROR_FLOW_CONTROL);
            SET(MAX_FRAME_SIZE, max_frame_size, 16384, 16777215, vhttp_HTTP2_ERROR_PROTOCOL);
#undef SET
        default:
            /* ignore unknown (5.5) */
            break;
        }
    }

    if (len != 0)
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    return 0;
}

uint8_t *vhttp_http2_encode_frame_header(uint8_t *dst, size_t length, uint8_t type, uint8_t flags, int32_t stream_id)
{
    if (length > 0xffffff)
        vhttp_fatal("invalid length");

    dst = vhttp_http2_encode24u(dst, (uint32_t)length);
    *dst++ = type;
    *dst++ = flags;
    dst = vhttp_http2_encode32u(dst, stream_id);

    return dst;
}

static uint8_t *allocate_frame(vhttp_buffer_t **buf, size_t length, uint8_t type, uint8_t flags, int32_t stream_id)
{
    vhttp_iovec_t alloced = vhttp_buffer_reserve(buf, vhttp_HTTP2_FRAME_HEADER_SIZE + length);
    (*buf)->size += vhttp_HTTP2_FRAME_HEADER_SIZE + length;
    return vhttp_http2_encode_frame_header((uint8_t *)alloced.base, length, type, flags, stream_id);
}

void vhttp_http2__encode_rst_stream_frame(vhttp_buffer_t **buf, uint32_t stream_id, int errnum)
{
    uint8_t *dst = allocate_frame(buf, 4, vhttp_HTTP2_FRAME_TYPE_RST_STREAM, 0, stream_id);
    dst = vhttp_http2_encode32u(dst, errnum);
}

void vhttp_http2_encode_ping_frame(vhttp_buffer_t **buf, int is_ack, const uint8_t *data)
{
    uint8_t *dst = allocate_frame(buf, 8, vhttp_HTTP2_FRAME_TYPE_PING, is_ack ? vhttp_HTTP2_FRAME_FLAG_ACK : 0, 0);
    memcpy(dst, data, 8);
    dst += 8;
}

void vhttp_http2_encode_goaway_frame(vhttp_buffer_t **buf, uint32_t last_stream_id, int errnum, vhttp_iovec_t additional_data)
{
    uint8_t *dst = allocate_frame(buf, 8 + additional_data.len, vhttp_HTTP2_FRAME_TYPE_GOAWAY, 0, 0);
    dst = vhttp_http2_encode32u(dst, last_stream_id);
    dst = vhttp_http2_encode32u(dst, (uint32_t)-errnum);
    vhttp_memcpy(dst, additional_data.base, additional_data.len);
}

void vhttp_http2_encode_settings_frame(vhttp_buffer_t **buf, vhttp_http2_settings_kvpair_t *settings, size_t num_settings)
{
    uint8_t *dst = allocate_frame(buf, 6 * num_settings, vhttp_HTTP2_FRAME_TYPE_SETTINGS, 0, 0);
    for (size_t i = 0; i < num_settings; ++i) {
        dst = vhttp_http2_encode16u(dst, settings[i].key);
        dst = vhttp_http2_encode32u(dst, settings[i].value);
    }
}

void vhttp_http2_encode_window_update_frame(vhttp_buffer_t **buf, uint32_t stream_id, int32_t window_size_increment)
{
    uint8_t *dst = allocate_frame(buf, 4, vhttp_HTTP2_FRAME_TYPE_WINDOW_UPDATE, 0, stream_id);
    dst = vhttp_http2_encode32u(dst, window_size_increment);
}

void vhttp_http2_encode_origin_frame(vhttp_buffer_t **buf, vhttp_iovec_t payload)
{
    uint8_t *dst = allocate_frame(buf, payload.len, vhttp_HTTP2_FRAME_TYPE_ORIGIN, 0, 0);
    memcpy(dst, payload.base, payload.len);
}

ssize_t vhttp_http2_decode_frame(vhttp_http2_frame_t *frame, const uint8_t *src, size_t len, size_t max_frame_size,
                               const char **err_desc)
{
    if (len < vhttp_HTTP2_FRAME_HEADER_SIZE)
        return vhttp_HTTP2_ERROR_INCOMPLETE;

    frame->length = vhttp_http2_decode24u(src);
    frame->type = src[3];
    frame->flags = src[4];
    frame->stream_id = vhttp_http2_decode32u(src + 5) & 0x7fffffff;

    if (frame->length > max_frame_size)
        return vhttp_HTTP2_ERROR_FRAME_SIZE;

    if (len < vhttp_HTTP2_FRAME_HEADER_SIZE + frame->length)
        return vhttp_HTTP2_ERROR_INCOMPLETE;

    frame->payload = src + vhttp_HTTP2_FRAME_HEADER_SIZE;

    return vhttp_HTTP2_FRAME_HEADER_SIZE + frame->length;
}

int vhttp_http2_decode_data_payload(vhttp_http2_data_payload_t *payload, const vhttp_http2_frame_t *frame, const char **err_desc)
{
    if (frame->stream_id == 0) {
        *err_desc = "invalid stream id in DATA frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }

    if ((frame->flags & vhttp_HTTP2_FRAME_FLAG_PADDED) != 0) {
        uint8_t padding_length;
        if (frame->length < 1) {
            *err_desc = "invalid DATA frame";
            return vhttp_HTTP2_ERROR_PROTOCOL;
        }
        padding_length = frame->payload[0];
        if (frame->length < 1 + padding_length) {
            *err_desc = "invalid DATA frame";
            return vhttp_HTTP2_ERROR_PROTOCOL;
        }
        payload->data = frame->payload + 1;
        payload->length = frame->length - (1 + padding_length);
    } else {
        payload->data = frame->payload;
        payload->length = frame->length;
    }
    return 0;
}

static const uint8_t *decode_priority(vhttp_http2_priority_t *priority, const uint8_t *src)
{
    uint32_t u4 = vhttp_http2_decode32u(src);
    src += 4;
    priority->exclusive = u4 >> 31;
    priority->dependency = u4 & 0x7fffffff;
    priority->weight = (uint16_t)*src++ + 1;
    return src;
}

int vhttp_http2_decode_headers_payload(vhttp_http2_headers_payload_t *payload, const vhttp_http2_frame_t *frame, const char **err_desc)
{
    const uint8_t *src = frame->payload, *src_end = frame->payload + frame->length;

    if (frame->stream_id == 0) {
        *err_desc = "invalid stream id in HEADERS frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }

    if ((frame->flags & vhttp_HTTP2_FRAME_FLAG_PADDED) != 0) {
        uint32_t padlen;
        if (src == src_end) {
            *err_desc = "invalid HEADERS frame";
            return vhttp_HTTP2_ERROR_PROTOCOL;
        }
        padlen = *src++;
        if (src_end - src < padlen) {
            *err_desc = "invalid HEADERS frame";
            return vhttp_HTTP2_ERROR_PROTOCOL;
        }
        src_end -= padlen;
    }

    if ((frame->flags & vhttp_HTTP2_FRAME_FLAG_PRIORITY) != 0) {
        if (src_end - src < 5)
            return -1;
        src = decode_priority(&payload->priority, src);
    } else {
        payload->priority = vhttp_http2_default_priority;
    }

    payload->headers = src;
    payload->headers_len = src_end - src;

    return 0;
}

int vhttp_http2_decode_priority_payload(vhttp_http2_priority_t *payload, const vhttp_http2_frame_t *frame, const char **err_desc)
{
    if (frame->stream_id == 0) {
        *err_desc = "invalid stream id in PRIORITY frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }
    if (frame->length != 5) {
        *err_desc = "invalid PRIORITY frame";
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    }

    decode_priority(payload, frame->payload);
    return 0;
}

int vhttp_http2_decode_rst_stream_payload(vhttp_http2_rst_stream_payload_t *payload, const vhttp_http2_frame_t *frame,
                                        const char **err_desc)
{
    if (frame->stream_id == 0) {
        *err_desc = "invalid stream id in RST_STREAM frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }
    if (frame->length != sizeof(payload->error_code)) {
        *err_desc = "invalid RST_STREAM frame";
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    }

    payload->error_code = vhttp_http2_decode32u(frame->payload);
    return 0;
}

int vhttp_http2_decode_ping_payload(vhttp_http2_ping_payload_t *payload, const vhttp_http2_frame_t *frame, const char **err_desc)
{
    if (frame->stream_id != 0) {
        *err_desc = "invalid PING frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }
    if (frame->length != sizeof(payload->data)) {
        *err_desc = "invalid PING frame";
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    }

    memcpy(payload->data, frame->payload, sizeof(payload->data));
    return 0;
}

int vhttp_http2_decode_goaway_payload(vhttp_http2_goaway_payload_t *payload, const vhttp_http2_frame_t *frame, const char **err_desc)
{
    if (frame->stream_id != 0) {
        *err_desc = "invalid stream id in GOAWAY frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }
    if (frame->length < 8) {
        *err_desc = "invalid GOAWAY frame";
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    }

    payload->last_stream_id = vhttp_http2_decode32u(frame->payload) & 0x7fffffff;
    payload->error_code = vhttp_http2_decode32u(frame->payload + 4);
    if ((payload->debug_data.len = frame->length - 8) != 0)
        payload->debug_data.base = (char *)frame->payload + 8;
    else
        payload->debug_data.base = NULL;

    return 0;
}

int vhttp_http2_decode_window_update_payload(vhttp_http2_window_update_payload_t *payload, const vhttp_http2_frame_t *frame,
                                           const char **err_desc, int *err_is_stream_level)
{
    if (frame->length != 4) {
        *err_is_stream_level = 0;
        *err_desc = "invalid WINDOW_UPDATE frame";
        return vhttp_HTTP2_ERROR_FRAME_SIZE;
    }

    payload->window_size_increment = vhttp_http2_decode32u(frame->payload) & 0x7fffffff;
    if (payload->window_size_increment == 0) {
        *err_is_stream_level = frame->stream_id != 0;
        *err_desc = "invalid WINDOW_UPDATE frame";
        return vhttp_HTTP2_ERROR_PROTOCOL;
    }

    return 0;
}
