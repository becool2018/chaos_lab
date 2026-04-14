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
 * @file ChaosLabSidecarHttp.hpp
 * @brief Minimal HTTP header parsing and WebSocket framing for Chaos Lab sidecar.
 *
 * Built out-of-tree; links libmessageengine.a from the messageEngine repository.
 *
 * Rules applied:
 *   - Power of 10: bounded buffers, checked I/O, no heap after init.
 *   - MISRA C++: no STL, no exceptions, static_cast only.
 */

#ifndef APP_CHAOS_LAB_SIDE_CAR_HTTP_HPP
#define APP_CHAOS_LAB_SIDE_CAR_HTTP_HPP

#include <cstddef>
#include <cstdint>

/// Send exactly @p len bytes (handles partial writes).
/// @return 0 on success, -1 on failure.
int cls_http_send_all(int fd, const void* data, size_t len);

/// Read until CRLFCRLF or buffer full. Sets non-blocking internally for the fd.
/// @return 0 on success, -1 on I/O error, -2 if headers too large.
int cls_http_recv_headers(int fd, char* buf, size_t cap, size_t* out_len);

/// Parse first line as "METHOD SP PATH SP HTTP/x".
/// @return 0 on success, -1 on parse failure.
int cls_http_parse_request_line(const char* buf,
                                char* method, size_t method_cap,
                                char* path, size_t path_cap);

/// Case-insensitive header lookup in a full header block (after first line).
/// @return 0 if found, -1 if not found.
int cls_http_header_value(const char* headers_block,
                          const char* header_name,
                          char* out, size_t out_cap);

/// Build HTTP 101 Switching Protocols including Sec-WebSocket-Accept.
/// @return length written (excluding NUL), or -1 on failure.
int cls_websocket_build_handshake_response(const char* request_headers,
                                           char* out_buf, size_t out_cap);

/// Send one server→client text WebSocket frame (FIN, opcode text, unmasked).
/// @return 0 on success, -1 on failure.
int cls_websocket_send_text(int fd, const char* text, size_t text_len);

/// Reply to a client ping with the same payload (RFC 6455).
/// @return 0 on success, -1 on failure.
int cls_websocket_send_pong(int fd, const uint8_t* ping_payload, size_t ping_len);

/// Consume one complete frame from @p buf / @p len_io. Client frames must be masked.
/// On text: writes NUL-terminated payload to @p out (max @p out_cap - 1 chars), sets @p out_len.
/// Ping frames are answered with pong (uses @p fd) and removed from the buffer.
/// @return 0 = text frame, 1 = incomplete buffer, -1 = close/error (non-text frames are skipped internally)
int cls_websocket_shift_one_text_frame(int fd, uint8_t* buf, size_t* len_io, size_t buf_cap,
                                       char* out, size_t out_cap, size_t* out_len);

/// Peek whether request is a WebSocket upgrade (Upgrade + Sec-WebSocket-Key).
/// @return 1 if WebSocket upgrade, 0 otherwise.
int cls_http_is_websocket_upgrade(const char* request_headers);

#endif // APP_CHAOS_LAB_SIDE_CAR_HTTP_HPP
