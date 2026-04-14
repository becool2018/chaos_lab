// Copyright 2026 Don Jessup
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @file ChaosLabSidecarHttp.cpp
 * @brief Minimal HTTP and WebSocket helpers for ChaosLabSidecar.
 *
 * Rules applied:
 *   - Power of 10: bounded loops, checked returns, no dynamic allocation.
 *   - MISRA C++: no STL, no exceptions.
 */

#include "ChaosLabSidecarHttp.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include <mbedtls/base64.h>
#include <mbedtls/md.h>

#include "core/Assert.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const char WS_MAGIC_GUID[] =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const uint32_t MAX_HEADER_RECV_CHUNKS = 1024U;

// ─────────────────────────────────────────────────────────────────────────────
// cls_http_send_all
// ─────────────────────────────────────────────────────────────────────────────

int cls_http_send_all(int fd, const void* data, size_t len)
{
    NEVER_COMPILED_OUT_ASSERT(fd >= 0);  // Assert: valid fd
    NEVER_COMPILED_OUT_ASSERT(data != nullptr || len == 0U);  // Assert: valid buffer

    const auto* p = static_cast<const uint8_t*>(data);
    size_t      sent = 0U;

    while (sent < len) {
        ssize_t n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += static_cast<size_t>(n);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_http_recv_headers
// ─────────────────────────────────────────────────────────────────────────────
// Branch count is driven by bounded poll/recv until CRLFCRLF; splitting would obscure I/O flow.

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int cls_http_recv_headers(int fd, char* buf, size_t cap, size_t* out_len)
{
    NEVER_COMPILED_OUT_ASSERT(fd >= 0);
    NEVER_COMPILED_OUT_ASSERT(buf != nullptr);
    NEVER_COMPILED_OUT_ASSERT(cap > 4U);
    NEVER_COMPILED_OUT_ASSERT(out_len != nullptr);

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return -1;
    }

    size_t total = 0U;
    for (uint32_t chunk = 0U; chunk < MAX_HEADER_RECV_CHUNKS; ++chunk) {
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, 5000);
        if (pr <= 0) {
            if (pr == 0) {
                return -1;  // timeout
            }
            return -1;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return -1;
        }
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        ssize_t n = ::recv(fd, buf + total, cap - 1U - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        total += static_cast<size_t>(n);
        buf[total] = '\0';

        if (total >= 4U) {
            for (size_t i = 0U; i + 3U < total; ++i) {
                if (buf[i] == '\r' && buf[i + 1U] == '\n' &&
                    buf[i + 2U] == '\r' && buf[i + 3U] == '\n') {
                    *out_len = total;
                    (void)fcntl(fd, F_SETFL, flags);
                    return 0;
                }
            }
        }
        if (total >= cap - 1U) {
            (void)fcntl(fd, F_SETFL, flags);
            return -2;
        }
    }
    (void)fcntl(fd, F_SETFL, flags);
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_http_parse_request_line
// ─────────────────────────────────────────────────────────────────────────────

int cls_http_parse_request_line(const char* buf,
                                char* method, size_t method_cap,
                                char* path, size_t path_cap)
{
    NEVER_COMPILED_OUT_ASSERT(buf != nullptr);
    NEVER_COMPILED_OUT_ASSERT(method != nullptr);
    NEVER_COMPILED_OUT_ASSERT(path != nullptr);
    NEVER_COMPILED_OUT_ASSERT(method_cap > 1U);
    NEVER_COMPILED_OUT_ASSERT(path_cap > 1U);

    size_t i = 0U;
    size_t mi = 0U;
    while (buf[i] != '\0' && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') {
        if (mi + 1U >= method_cap) {
            return -1;
        }
        method[mi] = buf[i];
        ++mi;
        ++i;
    }
    method[mi] = '\0';

    if (buf[i] != ' ') {
        return -1;
    }
    ++i;

    size_t pi = 0U;
    while (buf[i] != '\0' && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') {
        if (pi + 1U >= path_cap) {
            return -1;
        }
        path[pi] = buf[i];
        ++pi;
        ++i;
    }
    path[pi] = '\0';

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Line-wise header match (case-insensitive name)
// ─────────────────────────────────────────────────────────────────────────────

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static int header_line_matches(const char* line, size_t line_len, const char* name, size_t name_len)
{
    NEVER_COMPILED_OUT_ASSERT(line != nullptr);
    NEVER_COMPILED_OUT_ASSERT(name != nullptr);

    if (line_len < name_len + 1U) {
        return 0;
    }
    for (size_t j = 0U; j < name_len; ++j) {
        char c = line[j];
        char n = name[j];
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + 32);
        }
        if (n >= 'A' && n <= 'Z') {
            n = static_cast<char>(n + 32);
        }
        if (c != n) {
            return 0;
        }
    }
    if (line[name_len] != ':') {
        return 0;
    }
    return 1;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int cls_http_header_value(const char* headers_block,
                          const char* header_name,
                          char* out, size_t out_cap)
{
    NEVER_COMPILED_OUT_ASSERT(headers_block != nullptr);
    NEVER_COMPILED_OUT_ASSERT(header_name != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out_cap > 1U);

    const size_t name_len = strlen(header_name);
    size_t       i        = 0U;

    while (headers_block[i] != '\0') {
        size_t line_start = i;
        while (headers_block[i] != '\0' && headers_block[i] != '\r' && headers_block[i] != '\n') {
            ++i;
        }
        size_t line_end = i;
        size_t line_len = line_end - line_start;

        if (line_len > 0U &&
            header_line_matches(headers_block + line_start, line_len, header_name, name_len) != 0) {
            size_t pos = line_start + name_len + 1U;
            while (pos < line_end && headers_block[pos] == ' ') {
                ++pos;
            }
            size_t oi = 0U;
            while (pos < line_end && oi + 1U < out_cap) {
                out[oi] = headers_block[pos];
                ++oi;
                ++pos;
            }
            out[oi] = '\0';
            return 0;
        }

        while (headers_block[i] == '\r' || headers_block[i] == '\n') {
            ++i;
        }
        if (i == line_start) {
            ++i;
        }
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_http_is_websocket_upgrade
// ─────────────────────────────────────────────────────────────────────────────

int cls_http_is_websocket_upgrade(const char* request_headers)
{
    char upgrade[32];
    char key[128];

    if (cls_http_header_value(request_headers, "Upgrade", upgrade, sizeof(upgrade)) != 0) {
        return 0;
    }
    char u0 = upgrade[0];
    if (u0 >= 'A' && u0 <= 'Z') {
        u0 = static_cast<char>(u0 + 32);
    }
    if (u0 != 'w') {
        return 0;
    }
    if (strstr(upgrade, "websocket") == nullptr && strstr(upgrade, "WebSocket") == nullptr) {
        // already checked first char w — still verify substring loosely
        if (strstr(upgrade, "ebsocket") == nullptr) {
            return 0;
        }
    }

    if (cls_http_header_value(request_headers, "Sec-WebSocket-Key", key, sizeof(key)) != 0) {
        return 0;
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_websocket_build_handshake_response
// ─────────────────────────────────────────────────────────────────────────────

int cls_websocket_build_handshake_response(const char* request_headers,
                                           char* out_buf, size_t out_cap)
{
    char key[128];
    if (cls_http_header_value(request_headers, "Sec-WebSocket-Key", key, sizeof(key)) != 0) {
        return -1;
    }

    char concat[160];
    size_t ki = 0U;
    while (key[ki] != '\0' && ki < sizeof(concat) - sizeof(WS_MAGIC_GUID) - 2U) {
        concat[ki] = key[ki];
        ++ki;
    }
    concat[ki] = '\0';

    if (strlen(concat) + strlen(WS_MAGIC_GUID) >= sizeof(concat)) {
        return -1;
    }
    (void)strncat(concat, WS_MAGIC_GUID, sizeof(concat) - strlen(concat) - 1U);

    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == nullptr) {
        return -1;
    }

    unsigned char sha[20];
    int           sha_rc = mbedtls_md(md_info,
                                      reinterpret_cast<const unsigned char*>(concat),
                                      strlen(concat), sha);
    if (sha_rc != 0) {
        return -1;
    }

    unsigned char b64[64];
    size_t        b64_len = 0U;
    int           b64_rc  = mbedtls_base64_encode(b64, sizeof(b64), &b64_len, sha, 20U);
    if (b64_rc != 0) {
        return -1;
    }
    b64[b64_len] = '\0';

    int n = snprintf(out_buf, out_cap,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n"
                     "\r\n",
                     reinterpret_cast<const char*>(b64));
    if (n <= 0 || static_cast<size_t>(n) >= out_cap) {
        return -1;
    }
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_websocket_send_text
// ─────────────────────────────────────────────────────────────────────────────

int cls_websocket_send_text(int fd, const char* text, size_t text_len)
{
    NEVER_COMPILED_OUT_ASSERT(text != nullptr || text_len == 0U);

    uint8_t frame[8192];
    if (text_len + 8U > sizeof(frame)) {
        return -1;
    }

    size_t pos = 0U;
    frame[pos] = static_cast<uint8_t>(0x81U);  // FIN + text
    ++pos;

    if (text_len <= 125U) {
        frame[pos] = static_cast<uint8_t>(text_len);
        ++pos;
    } else if (text_len <= 65535U) {
        frame[pos] = 126U;
        ++pos;
        frame[pos] = static_cast<uint8_t>((text_len >> 8) & 0xFFU);
        ++pos;
        frame[pos] = static_cast<uint8_t>(text_len & 0xFFU);
        ++pos;
    } else {
        return -1;
    }

    for (size_t i = 0U; i < text_len; ++i) {
        frame[pos] = static_cast<uint8_t>(text[i]);
        ++pos;
    }

    return cls_http_send_all(fd, frame, pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_websocket_send_pong
// ─────────────────────────────────────────────────────────────────────────────

int cls_websocket_send_pong(int fd, const uint8_t* ping_payload, size_t ping_len)
{
    NEVER_COMPILED_OUT_ASSERT(ping_payload != nullptr || ping_len == 0U);

    uint8_t frame[8200];
    if (ping_len + 8U > sizeof(frame)) {
        return -1;
    }

    size_t pos = 0U;
    frame[pos] = static_cast<uint8_t>(0x8AU);  // FIN + pong
    ++pos;

    if (ping_len <= 125U) {
        frame[pos] = static_cast<uint8_t>(ping_len);
        ++pos;
    } else if (ping_len <= 65535U) {
        frame[pos] = 126U;
        ++pos;
        frame[pos] = static_cast<uint8_t>((ping_len >> 8) & 0xFFU);
        ++pos;
        frame[pos] = static_cast<uint8_t>(ping_len & 0xFFU);
        ++pos;
    } else {
        return -1;
    }

    for (size_t i = 0U; i < ping_len; ++i) {
        frame[pos] = ping_payload[i];
        ++pos;
    }

    return cls_http_send_all(fd, frame, pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// cls_websocket_shift_one_text_frame
// ─────────────────────────────────────────────────────────────────────────────

static void memmove_tail(uint8_t* buf, size_t* len_io, size_t drop_front, size_t buf_cap)
{
    if (drop_front >= *len_io) {
        *len_io = 0U;
        return;
    }
    size_t rest = *len_io - drop_front;
    if (drop_front > 0U) {
        (void)memmove(buf, buf + drop_front, rest);
    }
    *len_io = rest;
    (void)buf_cap;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int cls_websocket_shift_one_text_frame(int fd, uint8_t* buf, size_t* len_io, size_t buf_cap,
                                       char* out, size_t out_cap, size_t* out_len)
{
    NEVER_COMPILED_OUT_ASSERT(buf != nullptr);
    NEVER_COMPILED_OUT_ASSERT(len_io != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out_cap > 1U);
    NEVER_COMPILED_OUT_ASSERT(out_len != nullptr);

    if (*len_io > buf_cap) {
        return -1;
    }

    for (;;) {
        if (*len_io < 2U) {
            return 1;
        }

        const uint8_t b0 = buf[0];
        const uint8_t b1 = buf[1];
        const uint8_t opcode = static_cast<uint8_t>(b0 & 0x0FU);
        const bool    masked = (b1 & 0x80U) != 0U;

        size_t       pos       = 2U;
        uint64_t     payload64 = static_cast<uint64_t>(b1 & 0x7FU);

        if (payload64 == 126U) {
            if (*len_io < 4U) {
                return 1;
            }
            payload64 =
                (static_cast<uint64_t>(buf[2]) << 8) | static_cast<uint64_t>(buf[3]);
            pos = 4U;
        } else if (payload64 == 127U) {
            if (*len_io < 10U) {
                return 1;
            }
            payload64 = 0U;
            for (int bi = 0; bi < 8; ++bi) {
                payload64 = (payload64 << 8) | static_cast<uint64_t>(buf[2 + bi]);
            }
            pos = 10U;
        }

        if (!masked) {
            return -1;
        }
        if (payload64 > 65536ULL) {
            return -1;
        }
        const size_t payload_len = static_cast<size_t>(payload64);

        if (*len_io < pos + 4U + payload_len) {
            return 1;
        }

        uint8_t mask_key[4];
        mask_key[0] = buf[pos];
        mask_key[1] = buf[pos + 1U];
        mask_key[2] = buf[pos + 2U];
        mask_key[3] = buf[pos + 3U];
        pos += 4U;

        uint8_t* payload_ptr = buf + pos;

        for (size_t i = 0U; i < payload_len; ++i) {
            payload_ptr[i] = static_cast<uint8_t>(payload_ptr[i] ^ mask_key[i % 4U]);
        }

        const size_t frame_total = pos + payload_len;

        if (opcode == 0x8U) {
            return -1;
        }

        if (opcode == 0x9U) {
            if (cls_websocket_send_pong(fd, payload_ptr, payload_len) != 0) {
                return -1;
            }
            memmove_tail(buf, len_io, frame_total, buf_cap);
            continue;
        }

        if (opcode == 0xAU) {
            memmove_tail(buf, len_io, frame_total, buf_cap);
            continue;
        }

        if (opcode != 0x1U) {
            memmove_tail(buf, len_io, frame_total, buf_cap);
            continue;
        }

        if ((b0 & 0x80U) == 0U) {
            return -1;
        }

        if (payload_len + 1U >= out_cap) {
            return -1;
        }
        for (size_t i = 0U; i < payload_len; ++i) {
            out[i] = static_cast<char>(payload_ptr[i]);
        }
        out[payload_len] = '\0';
        *out_len         = payload_len;

        memmove_tail(buf, len_io, frame_total, buf_cap);
        return 0;
    }
}
