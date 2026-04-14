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
 * @file ChaosLabSidecar.cpp
 * @brief Chaos Lab HTTP/WebSocket sidecar over messageEngine (LOCAL_SIM + DeliveryEngine).
 *
 * Exposes REST endpoints matching chaos-lab/contracts/api-contract.ts and streams
 * DeliveryEngine observability events over WebSocket /api/events.
 *
 * Rules applied:
 *   - Power of 10: bounded buffers, fixed main-loop iterations, checked I/O.
 *   - MISRA C++: no STL, no exceptions, static_cast only.
 *   - F-Prime style: Result checks, Logger optional (uses printf for app-level traces).
 *
 * Built from chaos-lab/sidecar (out-of-tree); links messageEngine/build/libmessageengine.a.
 *
 * Usage: ./build/chaos_lab_sidecar [port]   (default port 8787)
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ChaosLabSidecarHttp.hpp"
#include "core/Assert.hpp"
#include "core/ChannelConfig.hpp"
#include "core/DeliveryEngine.hpp"
#include "core/DeliveryStats.hpp"
#include "core/DeliveryEvent.hpp"
#include "core/MessageEnvelope.hpp"
#include "core/Timestamp.hpp"
#include "core/Types.hpp"
#include "platform/LocalSimHarness.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static const uint16_t DEFAULT_PORT                 = 8787U;
static const uint32_t MAX_POLL_LOOPS             = 1000000U;
static const uint32_t HTTP_BUF_SIZE              = 16384U;
static const uint32_t JSON_OUT_SIZE              = 8192U;
static const uint32_t MAX_WS_CLIENTS             = 8U;
static const uint64_t TRAFFIC_PERIOD_US          = 250000ULL;  // send every 250 ms when running
static const NodeId   NODE_A                     = 1U;
static const NodeId   NODE_B                     = 2U;

// ─────────────────────────────────────────────────────────────────────────────
// Run state (maps to dashboard RunState)
// ─────────────────────────────────────────────────────────────────────────────

enum class SidecarRunState : uint8_t {
    IDLE      = 0U,
    RUNNING   = 1U,
    PAUSED    = 2U,
    COMPLETED = 3U,
    FAILED    = 4U,
};

// ─────────────────────────────────────────────────────────────────────────────
// Global simulation state
// ─────────────────────────────────────────────────────────────────────────────

static volatile sig_atomic_t g_stop_flag = 0;

static LocalSimHarness g_harness_a;
static LocalSimHarness g_harness_b;
static DeliveryEngine  g_engine_a;
static DeliveryEngine  g_engine_b;

static bool            g_sim_ready = false;
static SidecarRunState g_run_state = SidecarRunState::IDLE;
/// Wall-clock ms since Unix epoch (UTC) for run summary `started_at` / `ended_at`.
static uint64_t        g_run_started_at_ms = 0ULL;
static uint64_t        g_run_ended_at_ms   = 0ULL;
static uint64_t        g_last_traffic_us = 0U;
static uint64_t        g_msg_seq         = 1U;

static char            g_scenario_id[64]   = "default";
static char            g_scenario_name[128] = "Two-node link";
static ImpairmentConfig g_impairment{};

// WebSocket subscribers (per-client rx buffer + event filter)
enum class WsFilterChip : uint8_t {
    ALL = 0U,
    RETRIES,
    ACK_TIMEOUTS,
    DROPS,
    REORDERING,
    PARTITIONS,
    RESPONSES,
};

struct WsClientSlot {
    int          fd;
    WsFilterChip filter;
    uint8_t      rx[4096];
    size_t       rx_len;
};

static WsClientSlot g_ws_clients[MAX_WS_CLIENTS];
static uint32_t     g_ws_count = 0U;

// Event id for JSON
static uint64_t g_event_seq = 1U;

/// Access-Control-Allow-Origin value (default "*"; set CHAOS_LAB_CORS_ORIGIN for production).
static char g_cors_allow_origin[256] = "*";

static const char SIDE_CAR_VERSION[] = "0.1.0";

// ─────────────────────────────────────────────────────────────────────────────
// Signal handler
// ─────────────────────────────────────────────────────────────────────────────

static void signal_handler(int sig)
{
    (void)sig;
    g_stop_flag = 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON helpers (bounded snprintf)
// ─────────────────────────────────────────────────────────────────────────────

static int append_cors_and_json_headers(char* out, size_t cap, int status, size_t body_len)
{
    const char* status_text = "200 OK";
    if (status == 204) {
        status_text = "204 No Content";
    } else if (status == 400) {
        status_text = "400 Bad Request";
    } else if (status == 404) {
        status_text = "404 Not Found";
    }
    return snprintf(out, cap,
                    "HTTP/1.1 %s\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: %s\r\n"
                    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n",
                    status_text, g_cors_allow_origin, body_len);
}

static void init_cors_from_env()
{
    const char* e = getenv("CHAOS_LAB_CORS_ORIGIN");
    if ((e != nullptr) && (e[0] != '\0')) {
        size_t i = 0U;
        while ((e[i] != '\0') && (i + 1U < sizeof(g_cors_allow_origin))) {
            g_cors_allow_origin[i] = e[i];
            ++i;
        }
        g_cors_allow_origin[i] = '\0';
    } else {
        g_cors_allow_origin[0] = '*';
        g_cors_allow_origin[1] = '\0';
    }
}

static void kind_to_event_type(DeliveryEventKind k, char* out, size_t out_cap)
{
    NEVER_COMPILED_OUT_ASSERT(out != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out_cap > 24U);
    switch (k) {
    case DeliveryEventKind::SEND_OK:
        (void)snprintf(out, out_cap, "message_sent");
        break;
    case DeliveryEventKind::SEND_FAIL:
        (void)snprintf(out, out_cap, "send_fail");
        break;
    case DeliveryEventKind::ACK_RECEIVED:
        (void)snprintf(out, out_cap, "message_delivered");
        break;
    case DeliveryEventKind::RETRY_FIRED:
        (void)snprintf(out, out_cap, "retry");
        break;
    case DeliveryEventKind::ACK_TIMEOUT:
        (void)snprintf(out, out_cap, "ack_timeout");
        break;
    case DeliveryEventKind::DUPLICATE_DROP:
        (void)snprintf(out, out_cap, "duplicate_drop");
        break;
    case DeliveryEventKind::EXPIRY_DROP:
        (void)snprintf(out, out_cap, "expiry_drop");
        break;
    case DeliveryEventKind::MISROUTE_DROP:
        (void)snprintf(out, out_cap, "misroute_drop");
        break;
    default:
        (void)snprintf(out, out_cap, "message_sent");
        break;
    }
}

static void ws_remove_slot(uint32_t idx)
{
    if (idx >= g_ws_count) {
        return;
    }
    (void)close(g_ws_clients[idx].fd);
    g_ws_clients[idx] = g_ws_clients[g_ws_count - 1U];
    --g_ws_count;
}

static bool ws_type_matches_filter(const char* t, WsFilterChip f)
{
    if (f == WsFilterChip::ALL) {
        return true;
    }
    if (f == WsFilterChip::RETRIES) {
        return strcmp(t, "retry") == 0;
    }
    if (f == WsFilterChip::ACK_TIMEOUTS) {
        return strcmp(t, "ack_timeout") == 0;
    }
    if (f == WsFilterChip::DROPS) {
        return (strcmp(t, "duplicate_drop") == 0) || (strcmp(t, "expiry_drop") == 0) ||
               (strcmp(t, "misroute_drop") == 0) || (strcmp(t, "send_fail") == 0);
    }
    if (f == WsFilterChip::REORDERING) {
        return strcmp(t, "reordering") == 0;
    }
    if (f == WsFilterChip::PARTITIONS) {
        return strcmp(t, "partition") == 0;
    }
    if (f == WsFilterChip::RESPONSES) {
        return strcmp(t, "response") == 0;
    }
    return true;
}

static void ws_apply_filter_message(WsClientSlot* c, const char* msg)
{
    NEVER_COMPILED_OUT_ASSERT(c != nullptr);
    NEVER_COMPILED_OUT_ASSERT(msg != nullptr);

    if (strstr(msg, "\"subscribe\"") != nullptr) {
        c->filter = WsFilterChip::ALL;
        return;
    }

    char val[48];
    val[0] = '\0';

    const char* p = strstr(msg, "\"filter\"");
    if (p != nullptr) {
        p = strchr(p, ':');
        if (p != nullptr) {
            ++p;
            while ((*p == ' ') || (*p == '\t')) {
                ++p;
            }
            if (*p == '"') {
                ++p;
                size_t i = 0U;
                while ((p[i] != '\0') && (p[i] != '"') && (i + 1U < sizeof(val))) {
                    val[i] = p[i];
                    ++i;
                }
                val[i] = '\0';
            }
        }
    }

    if ((strstr(msg, "set_filter") == nullptr) && (val[0] == '\0')) {
        return;
    }

    if (val[0] == '\0') {
        c->filter = WsFilterChip::ALL;
        return;
    }
    if (strcmp(val, "all") == 0) {
        c->filter = WsFilterChip::ALL;
    } else if (strcmp(val, "retries") == 0) {
        c->filter = WsFilterChip::RETRIES;
    } else if (strcmp(val, "ack_timeouts") == 0) {
        c->filter = WsFilterChip::ACK_TIMEOUTS;
    } else if (strcmp(val, "drops") == 0) {
        c->filter = WsFilterChip::DROPS;
    } else if (strcmp(val, "reordering") == 0) {
        c->filter = WsFilterChip::REORDERING;
    } else if (strcmp(val, "partitions") == 0) {
        c->filter = WsFilterChip::PARTITIONS;
    } else if (strcmp(val, "responses") == 0) {
        c->filter = WsFilterChip::RESPONSES;
    } else {
        c->filter = WsFilterChip::ALL;
    }
}

static void ws_drain_incoming(uint32_t idx)
{
    if (idx >= g_ws_count) {
        return;
    }

    WsClientSlot* c = &g_ws_clients[idx];

    for (;;) {
        uint8_t chunk[2048];
        ssize_t n = ::recv(c->fd, chunk, sizeof(chunk), 0);
        if (n < 0) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                return;
            }
            ws_remove_slot(idx);
            return;
        }
        if (n == 0) {
            ws_remove_slot(idx);
            return;
        }

        const size_t add = static_cast<size_t>(n);
        if (c->rx_len + add > sizeof(c->rx)) {
            ws_remove_slot(idx);
            return;
        }
        (void)memcpy(c->rx + c->rx_len, chunk, add);
        c->rx_len += add;

        for (;;) {
            char     text[2048];
            size_t   tlen = 0U;
            const int pr =
                cls_websocket_shift_one_text_frame(c->fd, c->rx, &c->rx_len, sizeof(c->rx), text,
                                                     sizeof(text), &tlen);
            if (pr == 1) {
                break;
            }
            if (pr == -1) {
                ws_remove_slot(idx);
                return;
            }
            if (pr == 0) {
                ws_apply_filter_message(c, text);
            }
        }
    }
}

static void broadcast_ws_event(const char* json, size_t json_len, const char* event_type)
{
    uint32_t w = 0U;
    while (w < g_ws_count) {
        if (!ws_type_matches_filter(event_type, g_ws_clients[w].filter)) {
            ++w;
            continue;
        }
        if (cls_websocket_send_text(g_ws_clients[w].fd, json, json_len) != 0) {
            ws_remove_slot(w);
        } else {
            ++w;
        }
    }
}

static void drain_engine_events(DeliveryEngine& eng)
{
    DeliveryEvent ev;
    for (;;) {
        Result r = eng.poll_event(ev);
        if (r == Result::ERR_EMPTY) {
            break;
        }
        if (r != Result::OK) {
            break;
        }

        char type_buf[32];
        kind_to_event_type(ev.kind, type_buf, sizeof(type_buf));

        char json[JSON_OUT_SIZE];
        char summary[160];
        (void)snprintf(summary, sizeof(summary), "delivery event kind=%u msg_id=%llu",
                       static_cast<unsigned int>(static_cast<uint8_t>(ev.kind)),
                       static_cast<unsigned long long>(ev.message_id));

        int n = snprintf(json, sizeof(json),
                         "{\"event\":{\"id\":\"%llu\",\"ts\":\"%llu\",\"type\":\"%s\","
                         "\"summary\":\"%s\",\"detail\":{\"result\":%u}}}\n",
                         static_cast<unsigned long long>(g_event_seq),
                         static_cast<unsigned long long>(ev.timestamp_us),
                         type_buf,
                         summary,
                         static_cast<unsigned int>(static_cast<uint8_t>(ev.result)));
        if (n > 0 && static_cast<size_t>(n) < sizeof(json)) {
            ++g_event_seq;
            broadcast_ws_event(json, static_cast<size_t>(n), type_buf);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulation setup / tick
// ─────────────────────────────────────────────────────────────────────────────

static void make_data_envelope(MessageEnvelope& env, NodeId src, NodeId dst, uint64_t msg_id)
{
    envelope_init(env);
    env.message_type      = MessageType::DATA;
    env.message_id        = msg_id;
    env.timestamp_us      = timestamp_now_us();
    env.source_id         = src;
    env.destination_id    = dst;
    env.priority          = 0U;
    env.reliability_class = ReliabilityClass::RELIABLE_RETRY;
    env.expiry_time_us    = env.timestamp_us + 10000000ULL;  // +10 s
    env.payload_length    = 4U;
    env.payload[0]        = 0xAAU;
    env.payload[1]        = 0xBBU;
    env.payload[2]        = 0xCCU;
    env.payload[3]        = 0xDDU;
}

static Result sim_init()
{
    g_harness_a.close();
    g_harness_b.close();
    g_sim_ready = false;

    TransportConfig cfg_a;
    transport_config_default(cfg_a);
    cfg_a.kind                       = TransportKind::LOCAL_SIM;
    cfg_a.local_node_id              = NODE_A;
    cfg_a.is_server                  = false;
    cfg_a.channels[0].reliability    = ReliabilityClass::RELIABLE_RETRY;
    cfg_a.channels[0].max_retries    = 5U;
    cfg_a.channels[0].retry_backoff_ms = 80U;
    cfg_a.channels[0].recv_timeout_ms  = 500U;
    cfg_a.channels[0].impairment     = g_impairment;

    Result ra = g_harness_a.init(cfg_a);
    if (ra != Result::OK) {
        return ra;
    }

    TransportConfig cfg_b;
    transport_config_default(cfg_b);
    cfg_b.kind                       = TransportKind::LOCAL_SIM;
    cfg_b.local_node_id              = NODE_B;
    cfg_b.is_server                  = false;
    cfg_b.channels[0].reliability    = ReliabilityClass::RELIABLE_RETRY;
    cfg_b.channels[0].max_retries    = 5U;
    cfg_b.channels[0].retry_backoff_ms = 80U;
    cfg_b.channels[0].recv_timeout_ms  = 500U;
    cfg_b.channels[0].impairment     = g_impairment;

    Result rb = g_harness_b.init(cfg_b);
    if (rb != Result::OK) {
        g_harness_a.close();
        return rb;
    }

    g_harness_a.link(&g_harness_b);
    g_harness_b.link(&g_harness_a);

    g_engine_a.init(&g_harness_a, cfg_a.channels[0], NODE_A);
    g_engine_b.init(&g_harness_b, cfg_b.channels[0], NODE_B);

    g_sim_ready = true;
    g_last_traffic_us = timestamp_now_us();
    return Result::OK;
}

static void sim_close()
{
    g_harness_a.close();
    g_harness_b.close();
    g_sim_ready = false;
}

static void sim_tick()
{
    if (!g_sim_ready) {
        return;
    }
    if (g_run_state != SidecarRunState::RUNNING) {
        return;
    }

    uint64_t now_us = timestamp_now_us();

    MessageEnvelope recv_env;
    (void)g_engine_b.receive(recv_env, 0U, now_us);
    (void)g_engine_a.receive(recv_env, 0U, now_us);

    (void)g_engine_a.pump_retries(now_us);
    (void)g_engine_b.pump_retries(now_us);
    (void)g_engine_a.sweep_ack_timeouts(now_us);
    (void)g_engine_b.sweep_ack_timeouts(now_us);

    if ((now_us - g_last_traffic_us) >= TRAFFIC_PERIOD_US) {
        MessageEnvelope send_env;
        make_data_envelope(send_env, NODE_A, NODE_B, g_msg_seq);
        ++g_msg_seq;
        (void)g_engine_a.send(send_env, now_us);
        g_last_traffic_us = now_us;
    }

    drain_engine_events(g_engine_a);
    drain_engine_events(g_engine_b);
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON parse helpers (minimal key/value scan)
// ─────────────────────────────────────────────────────────────────────────────

static bool parse_json_double(const char* body, const char* key, double* out)
{
    NEVER_COMPILED_OUT_ASSERT(body != nullptr);
    NEVER_COMPILED_OUT_ASSERT(key != nullptr);
    NEVER_COMPILED_OUT_ASSERT(out != nullptr);

    const char* p = strstr(body, key);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, ':');
    if (p == nullptr) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    char* end = nullptr;
    *out      = strtod(p, &end);
    return end != p;
}

static bool parse_json_uint32(const char* body, const char* key, uint32_t* out)
{
    const char* p = strstr(body, key);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, ':');
    if (p == nullptr) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    char* end = nullptr;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

static bool parse_json_bool_key(const char* body, const char* key, bool* out)
{
    const char* p = strstr(body, key);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p, ':');
    if (p == nullptr) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (p[0] == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
        *out = true;
        return true;
    }
    if (p[0] == 'f' && p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        *out = false;
        return true;
    }
    return false;
}

static void apply_impairment_from_body(const char* body)
{
    impairment_config_default(g_impairment);
    g_impairment.enabled = true;

    double d = 0.0;
    if (parse_json_double(body, "\"loss_probability\"", &d)) {
        g_impairment.loss_probability = d;
    }
    if (parse_json_double(body, "\"fixed_latency_ms\"", &d)) {
        g_impairment.fixed_latency_ms = static_cast<uint32_t>(d);
    }
    if (parse_json_double(body, "\"jitter_mean_ms\"", &d)) {
        g_impairment.jitter_mean_ms = static_cast<uint32_t>(d);
    }
    if (parse_json_double(body, "\"jitter_variance_ms\"", &d)) {
        g_impairment.jitter_variance_ms = static_cast<uint32_t>(d);
    }
    if (parse_json_double(body, "\"duplication_probability\"", &d)) {
        g_impairment.duplication_probability = d;
    }
    bool b = false;
    if (parse_json_bool_key(body, "\"reorder_enabled\"", &b)) {
        g_impairment.reorder_enabled = b;
    }
    uint32_t u32 = 0U;
    if (parse_json_uint32(body, "\"reorder_window_size\"", &u32)) {
        g_impairment.reorder_window_size = u32;
    }
    if (parse_json_bool_key(body, "\"partition_enabled\"", &b)) {
        g_impairment.partition_enabled = b;
    }
    if (parse_json_uint32(body, "\"partition_duration_ms\"", &u32)) {
        g_impairment.partition_duration_ms = u32;
    }
    if (parse_json_uint32(body, "\"partition_gap_ms\"", &u32)) {
        g_impairment.partition_gap_ms = u32;
    }

    // ImpairmentEngine rejects partition_enabled with partition_gap_ms==0 (MED-7).
    if (g_impairment.partition_enabled && (g_impairment.partition_gap_ms == 0U)) {
        g_impairment.partition_gap_ms = 1U;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP response builders
// ─────────────────────────────────────────────────────────────────────────────

static int send_http_text(int fd, int status, const char* body, size_t body_len)
{
    char head[768];
    int  hl = append_cors_and_json_headers(head, sizeof(head), status, body_len);
    if (hl <= 0 || static_cast<size_t>(hl) >= sizeof(head)) {
        return -1;
    }
    if (cls_http_send_all(fd, head, static_cast<size_t>(hl)) != 0) {
        return -1;
    }
    if (body_len > 0U && body != nullptr) {
        if (cls_http_send_all(fd, body, body_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int handle_options(int fd)
{
    char resp[512];
    int  n = snprintf(resp, sizeof(resp),
                      "HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: %s\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type\r\n"
                      "Content-Length: 0\r\n"
                      "\r\n",
                      g_cors_allow_origin);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(resp)) {
        return -1;
    }
    return cls_http_send_all(fd, resp, static_cast<size_t>(n));
}

static int build_presets_json(char* out, size_t cap)
{
    // Static presets aligned with chaos-lab UI names (JSON numeric probabilities).
    return snprintf(out, cap,
                    "["
                    "{\"id\":\"healthy\",\"name\":\"Healthy\",\"description\":\"No impairments\","
                    "\"impairments\":{\"loss_probability\":0,\"fixed_latency_ms\":0,"
                    "\"jitter_mean_ms\":0,\"jitter_variance_ms\":0,\"duplication_probability\":0,"
                    "\"reorder_enabled\":false,\"reorder_window_size\":0,"
                    "\"partition_enabled\":false,\"partition_duration_ms\":0,\"partition_gap_ms\":0}},"
                    "{\"id\":\"lossy\",\"name\":\"Lossy\",\"description\":\"Random loss\","
                    "\"impairments\":{\"loss_probability\":0.2,\"fixed_latency_ms\":0,"
                    "\"jitter_mean_ms\":0,\"jitter_variance_ms\":0,\"duplication_probability\":0,"
                    "\"reorder_enabled\":false,\"reorder_window_size\":0,"
                    "\"partition_enabled\":false,\"partition_duration_ms\":0,\"partition_gap_ms\":0}},"
                    "{\"id\":\"high_latency\",\"name\":\"High Latency\",\"description\":\"Extra delay\","
                    "\"impairments\":{\"loss_probability\":0,\"fixed_latency_ms\":200,"
                    "\"jitter_mean_ms\":0,\"jitter_variance_ms\":0,\"duplication_probability\":0,"
                    "\"reorder_enabled\":false,\"reorder_window_size\":0,"
                    "\"partition_enabled\":false,\"partition_duration_ms\":0,\"partition_gap_ms\":0}}"
                    "]");
}

static int build_scenario_json(char* out, size_t cap)
{
    return snprintf(out, cap,
                    "{\"scenario\":{\"id\":\"%s\",\"name\":\"%s\","
                    "\"base_impairments\":{"
                    "\"loss_probability\":%.6f,\"fixed_latency_ms\":%u,"
                    "\"jitter_mean_ms\":%u,\"jitter_variance_ms\":%u,"
                    "\"duplication_probability\":%.6f,"
                    "\"reorder_enabled\":%s,\"reorder_window_size\":%u,"
                    "\"partition_enabled\":%s,\"partition_duration_ms\":%u,\"partition_gap_ms\":%u"
                    "}}}",
                    g_scenario_id, g_scenario_name,
                    g_impairment.loss_probability,
                    g_impairment.fixed_latency_ms,
                    g_impairment.jitter_mean_ms,
                    g_impairment.jitter_variance_ms,
                    g_impairment.duplication_probability,
                    g_impairment.reorder_enabled ? "true" : "false",
                    g_impairment.reorder_window_size,
                    g_impairment.partition_enabled ? "true" : "false",
                    g_impairment.partition_duration_ms,
                    g_impairment.partition_gap_ms);
}

static uint64_t wall_clock_ms_utc()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0ULL;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

static void format_iso8601_utc(char* buf, size_t cap, uint64_t unix_ms)
{
    if ((buf == nullptr) || (cap == 0U)) {
        return;
    }
    if (unix_ms == 0ULL) {
        buf[0] = '\0';
        return;
    }
    const time_t   sec = static_cast<time_t>(unix_ms / 1000ULL);
    const unsigned ms  = static_cast<unsigned>(unix_ms % 1000ULL);
    struct tm      tm_utc {};
    (void)gmtime_r(&sec, &tm_utc);
    (void)snprintf(buf,
                   cap,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
                   tm_utc.tm_year + 1900,
                   tm_utc.tm_mon + 1,
                   tm_utc.tm_mday,
                   tm_utc.tm_hour,
                   tm_utc.tm_min,
                   tm_utc.tm_sec,
                   ms);
}

static int build_topology_json(char* out, size_t cap)
{
    // Keeps HTTP topology aligned with LOCAL_SIM harness nodes (see NODE_A / NODE_B).
    return snprintf(out, cap,
                    "{\"nodes\":[{\"id\":\"%u\",\"label\":\"Node A\"},{\"id\":\"%u\",\"label\":\"Node B\"}],"
                    "\"links\":[{\"from\":\"%u\",\"to\":\"%u\"}]}",
                    static_cast<unsigned int>(NODE_A),
                    static_cast<unsigned int>(NODE_B),
                    static_cast<unsigned int>(NODE_A),
                    static_cast<unsigned int>(NODE_B));
}

static const char* run_state_cstr()
{
    if (g_run_state == SidecarRunState::RUNNING) {
        return "running";
    }
    if (g_run_state == SidecarRunState::PAUSED) {
        return "paused";
    }
    if (g_run_state == SidecarRunState::COMPLETED) {
        return "completed";
    }
    if (g_run_state == SidecarRunState::FAILED) {
        return "failed";
    }
    return "idle";
}

static int build_health_json(char* out, size_t cap)
{
    return snprintf(out, cap,
                    "{\"ok\":true,\"version\":\"%s\",\"sim_ready\":%s,\"run_state\":\"%s\"}",
                    SIDE_CAR_VERSION,
                    g_sim_ready ? "true" : "false",
                    run_state_cstr());
}

static int build_summary_json(char* out, size_t cap)
{
    DeliveryStats ds;
    delivery_stats_init(ds);
    if (g_sim_ready) {
        g_engine_a.get_stats(ds);
    }

    const char* rs = run_state_cstr();

    uint32_t avg_lat_ms = 0U;
    if (ds.latency_sample_count > 0U) {
        avg_lat_ms = static_cast<uint32_t>((ds.latency_sum_us / ds.latency_sample_count) / 1000ULL);
    }

    char started_iso[48];
    char ended_frag[96];
    format_iso8601_utc(started_iso, sizeof(started_iso), g_run_started_at_ms);
    ended_frag[0] = '\0';
    if ((g_run_ended_at_ms > 0ULL) && (g_run_state != SidecarRunState::RUNNING)) {
        char e[48];
        format_iso8601_utc(e, sizeof(e), g_run_ended_at_ms);
        (void)snprintf(ended_frag, sizeof(ended_frag), ",\"ended_at\":\"%s\"", e);
    }

    return snprintf(out,
                    cap,
                    "{"
                    "\"scenario\":{\"id\":\"%s\",\"name\":\"%s\","
                    "\"base_impairments\":{"
                    "\"loss_probability\":%.6f,\"fixed_latency_ms\":%u,"
                    "\"jitter_mean_ms\":%u,\"jitter_variance_ms\":%u,"
                    "\"duplication_probability\":%.6f,"
                    "\"reorder_enabled\":%s,\"reorder_window_size\":%u,"
                    "\"partition_enabled\":%s,\"partition_duration_ms\":%u,\"partition_gap_ms\":%u"
                    "}},"
                    "\"run_state\":\"%s\","
                    "\"started_at\":\"%s\""
                    "%s"
                    ",\"metrics\":{"
                    "\"messages_sent\":%u,\"messages_delivered\":%u,"
                    "\"retries\":%u,\"ack_timeouts\":%u,"
                    "\"duplicates_dropped\":%u,\"expiry_drops\":%u,"
                    "\"average_latency_ms\":%u,\"in_flight_messages\":%u"
                    "}}",
                    g_scenario_id,
                    g_scenario_name,
                    g_impairment.loss_probability,
                    g_impairment.fixed_latency_ms,
                    g_impairment.jitter_mean_ms,
                    g_impairment.jitter_variance_ms,
                    g_impairment.duplication_probability,
                    g_impairment.reorder_enabled ? "true" : "false",
                    g_impairment.reorder_window_size,
                    g_impairment.partition_enabled ? "true" : "false",
                    g_impairment.partition_duration_ms,
                    g_impairment.partition_gap_ms,
                    rs,
                    started_iso,
                    ended_frag,
                    ds.msgs_sent,
                    ds.msgs_received,
                    ds.retry.retries_sent,
                    ds.ack.timeouts,
                    ds.msgs_dropped_duplicate,
                    ds.msgs_dropped_expired,
                    avg_lat_ms,
                    0U);
}

static const char* path_after_api(const char* path)
{
    const char* p = strstr(path, "/api/");
    if (p == nullptr) {
        return path;
    }
    return p + 5;
}

static int send_json_from_builder(int cfd, int (*builder)(char*, size_t))
{
    char json[JSON_OUT_SIZE];
    int  n = builder(json, sizeof(json));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(json)) {
        return -1;
    }
    return send_http_text(cfd, 200, json, static_cast<size_t>(n));
}

static int route_get(int cfd, const char* sub)
{
    if ((strcmp(sub, "health") == 0) || (strcmp(sub, "/health") == 0)) {
        return send_json_from_builder(cfd, build_health_json);
    }
    if (strcmp(sub, "presets") == 0) {
        return send_json_from_builder(cfd, build_presets_json);
    }
    if (strcmp(sub, "scenario/current") == 0) {
        return send_json_from_builder(cfd, build_scenario_json);
    }
    if (strcmp(sub, "topology") == 0) {
        return send_json_from_builder(cfd, build_topology_json);
    }
    if (strcmp(sub, "run/summary") == 0) {
        return send_json_from_builder(cfd, build_summary_json);
    }
    static const char NF[] = "{\"error\":\"not found\"}";
    return send_http_text(cfd, 404, NF, strlen(NF));
}

static int route_post_scenario_current(int cfd, const char* body)
{
    apply_impairment_from_body(body);
    if (g_sim_ready) {
        sim_close();
    }
    if (sim_init() != Result::OK) {
        static const char ERR[] = "{\"error\":\"sim_init failed\"}";
        return send_http_text(cfd, 400, ERR, strlen(ERR));
    }
    char json[JSON_OUT_SIZE];
    int  n = build_scenario_json(json, sizeof(json));
    return send_http_text(cfd, 200, json, static_cast<size_t>(n));
}

static int route_post_scenario_save(int cfd, const char* body)
{
    apply_impairment_from_body(body);
    char json[JSON_OUT_SIZE];
    int  n = build_scenario_json(json, sizeof(json));
    return send_http_text(cfd, 200, json, static_cast<size_t>(n));
}

static int route_post_run_start(int cfd)
{
    if (!g_sim_ready) {
        if (sim_init() != Result::OK) {
            static const char ERR[] = "{\"error\":\"sim_init failed\"}";
            return send_http_text(cfd, 400, ERR, strlen(ERR));
        }
    }
    if (g_run_state != SidecarRunState::RUNNING) {
        g_run_started_at_ms = wall_clock_ms_utc();
        g_run_ended_at_ms   = 0ULL;
    }
    g_run_state       = SidecarRunState::RUNNING;
    g_last_traffic_us = timestamp_now_us();
    static const char OKJ[] = "{\"run_state\":\"running\"}";
    return send_http_text(cfd, 200, OKJ, strlen(OKJ));
}

static int route_post_run_reset(int cfd)
{
    g_run_state         = SidecarRunState::IDLE;
    g_run_started_at_ms = 0ULL;
    g_run_ended_at_ms   = 0ULL;
    sim_close();
    if (sim_init() != Result::OK) {
        static const char ERR[] = "{\"error\":\"sim_init failed\"}";
        return send_http_text(cfd, 400, ERR, strlen(ERR));
    }
    static const char OKJ[] = "{\"run_state\":\"idle\"}";
    return send_http_text(cfd, 200, OKJ, strlen(OKJ));
}

static int route_post_run_replay(int cfd)
{
    g_run_started_at_ms = wall_clock_ms_utc();
    g_run_ended_at_ms     = 0ULL;
    g_run_state           = SidecarRunState::RUNNING;
    sim_close();
    if (sim_init() != Result::OK) {
        static const char ERR[] = "{\"error\":\"sim_init failed\"}";
        return send_http_text(cfd, 400, ERR, strlen(ERR));
    }
    g_last_traffic_us = timestamp_now_us();
    static const char OKJ[] = "{\"run_state\":\"running\"}";
    return send_http_text(cfd, 200, OKJ, strlen(OKJ));
}

static int route_post(int cfd, const char* sub, const char* body)
{
    if (strcmp(sub, "scenario/current") == 0) {
        return route_post_scenario_current(cfd, body);
    }
    if (strcmp(sub, "scenario/save") == 0) {
        return route_post_scenario_save(cfd, body);
    }
    if (strcmp(sub, "run/start") == 0) {
        return route_post_run_start(cfd);
    }
    if (strcmp(sub, "run/pause") == 0) {
        if (g_run_state == SidecarRunState::RUNNING) {
            g_run_ended_at_ms = wall_clock_ms_utc();
        }
        g_run_state = SidecarRunState::PAUSED;
        static const char OKJ[] = "{\"run_state\":\"paused\"}";
        return send_http_text(cfd, 200, OKJ, strlen(OKJ));
    }
    if (strcmp(sub, "run/reset") == 0) {
        return route_post_run_reset(cfd);
    }
    if (strcmp(sub, "run/replay") == 0) {
        return route_post_run_replay(cfd);
    }
    static const char NF[] = "{\"error\":\"not found\"}";
    return send_http_text(cfd, 404, NF, strlen(NF));
}

static int try_websocket_upgrade(int cfd, const char* method, const char* path, const char* header_buf)
{
    if (strcmp(method, "GET") != 0) {
        return 0;
    }
    if (strstr(path, "/api/events") == nullptr) {
        return 0;
    }
    if (cls_http_is_websocket_upgrade(header_buf) == 0) {
        return 0;
    }

    char wsbuf[512];
    int  wn = cls_websocket_build_handshake_response(header_buf, wsbuf, sizeof(wsbuf));
    if (wn <= 0) {
        return -1;
    }
    if (cls_http_send_all(cfd, wsbuf, static_cast<size_t>(wn)) != 0) {
        return -1;
    }
    if (g_ws_count < MAX_WS_CLIENTS) {
        WsClientSlot* s = &g_ws_clients[g_ws_count];
        s->fd         = cfd;
        s->filter     = WsFilterChip::ALL;
        s->rx_len     = 0U;
        int fl = fcntl(cfd, F_GETFL, 0);
        if (fl >= 0) {
            (void)fcntl(cfd, F_SETFL, fl | O_NONBLOCK);
        }
        ++g_ws_count;
        return 1;
    }
    static const char FULL[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    (void)cls_http_send_all(cfd, FULL, strlen(FULL));
    return 0;
}

static int read_post_body(int cfd, char* buf, size_t hdr_len, size_t content_length)
{
    const char* sep = strstr(buf, "\r\n\r\n");
    if (sep == nullptr) {
        return -1;
    }
    size_t header_end = static_cast<size_t>((sep - buf) + 4);
    size_t have_body  = 0U;
    if (hdr_len > header_end) {
        have_body = hdr_len - header_end;
    }

    if (content_length == 0U) {
        buf[header_end] = '\0';
        return static_cast<int>(header_end);
    }

    if (header_end + content_length >= HTTP_BUF_SIZE) {
        return -2;
    }
    while (have_body < content_length) {
        ssize_t n = ::recv(cfd, buf + header_end + have_body,
                           HTTP_BUF_SIZE - 1U - (header_end + have_body), 0);
        if (n <= 0) {
            return -1;
        }
        have_body += static_cast<size_t>(n);
    }
    buf[header_end + content_length] = '\0';
    return static_cast<int>(header_end);
}

static int dispatch_method(int cfd, const char* method, const char* path, const char* body)
{
    const char* sub = path_after_api(path);
    if (strcmp(method, "GET") == 0) {
        return route_get(cfd, sub);
    }
    if (strcmp(method, "POST") == 0) {
        return route_post(cfd, sub, body);
    }
    static const char NF[] = "{\"error\":\"not found\"}";
    return send_http_text(cfd, 404, NF, strlen(NF));
}

// HTTP keep-alive not used; one request per connection — orchestration stays explicit here.

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static int handle_http_client(int cfd)
{
    char   buf[HTTP_BUF_SIZE];
    size_t hdr_len = 0U;
    int    rr      = cls_http_recv_headers(cfd, buf, sizeof(buf), &hdr_len);
    if (rr != 0) {
        return -1;
    }

    char method[16];
    char path[256];
    if (cls_http_parse_request_line(buf, method, sizeof(method), path, sizeof(path)) != 0) {
        return -1;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        return handle_options(cfd);
    }

    int ws_rc = try_websocket_upgrade(cfd, method, path, buf);
    if (ws_rc != 0) {
        return ws_rc;
    }

    size_t content_length = 0U;
    char   cl_buf[32];
    if (cls_http_header_value(buf, "Content-Length", cl_buf, sizeof(cl_buf)) == 0) {
        char* end = nullptr;
        content_length = static_cast<size_t>(strtoul(cl_buf, &end, 10));
    }

    const char* body_ptr = buf;
    if (strcmp(method, "POST") == 0) {
        int he = read_post_body(cfd, buf, hdr_len, content_length);
        if (he == -2) {
            static const char ERR[] = "{\"error\":\"body too large\"}";
            (void)send_http_text(cfd, 400, ERR, strlen(ERR));
            return 0;
        }
        if (he < 0) {
            return -1;
        }
        body_ptr = buf + static_cast<size_t>(he);
    } else {
        const char* sep = strstr(buf, "\r\n\r\n");
        if (sep != nullptr) {
            body_ptr = buf + static_cast<size_t>((sep - buf) + 4);
        }
    }

    return dispatch_method(cfd, method, path, body_ptr);
}


// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static uint16_t parse_port(int argc, char* const argv[])
{
    if (argc >= 2) {
        char* end         = nullptr;
        unsigned long v = strtoul(argv[1], &end, 10);
        if ((end != argv[1]) && (v > 0UL) && (v < 65536UL)) {
            return static_cast<uint16_t>(v);
        }
    }
    const char* pe = getenv("CHAOS_LAB_PORT");
    if ((pe != nullptr) && (pe[0] != '\0')) {
        char* end         = nullptr;
        unsigned long v = strtoul(pe, &end, 10);
        if ((end != pe) && (v > 0UL) && (v < 65536UL)) {
            return static_cast<uint16_t>(v);
        }
    }
    return DEFAULT_PORT;
}

static int setup_listen_socket(uint16_t port)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return -1;
    }
    int opt = 1;
    (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        (void)close(listen_fd);
        return -1;
    }
    if (listen(listen_fd, 8) != 0) {
        (void)close(listen_fd);
        return -1;
    }

    int lflags = fcntl(listen_fd, F_GETFL, 0);
    if (lflags >= 0) {
        (void)fcntl(listen_fd, F_SETFL, lflags | O_NONBLOCK);
    }
    return listen_fd;
}

static void accept_one_client(int listen_fd)
{
    sockaddr_in cli{};
    socklen_t   clen = sizeof(cli);
    int         cfd  = accept(listen_fd, reinterpret_cast<sockaddr*>(&cli), &clen);
    if (cfd < 0) {
        return;
    }
    int h = handle_http_client(cfd);
    if (h == 0) {
        (void)close(cfd);
    }
}

static void run_main_loop(int listen_fd)
{
    struct pollfd pfds[1U + MAX_WS_CLIENTS];

    for (uint32_t iter = 0U; (iter < MAX_POLL_LOOPS) && (g_stop_flag == 0); ++iter) {
        sim_tick();

        uint32_t nfds = 0U;
        pfds[nfds].fd      = listen_fd;
        pfds[nfds].events  = POLLIN;
        pfds[nfds].revents = 0;
        ++nfds;

        for (uint32_t i = 0U; i < g_ws_count; ++i) {
            pfds[nfds].fd      = g_ws_clients[i].fd;
            pfds[nfds].events  = POLLIN;
            pfds[nfds].revents = 0;
            ++nfds;
        }

        const int pr = poll(pfds, static_cast<nfds_t>(nfds), 50);
        if (pr < 0) {
            break;
        }
        if (pr == 0) {
            continue;
        }

        if ((pfds[0].revents & POLLIN) != 0) {
            accept_one_client(listen_fd);
        }

        uint32_t i = 0U;
        while (i < g_ws_count) {
            if ((pfds[i + 1U].revents & (POLLIN | POLLERR | POLLHUP)) == 0) {
                ++i;
                continue;
            }
            const uint32_t before = g_ws_count;
            ws_drain_incoming(i);
            if (g_ws_count < before) {
                continue;
            }
            ++i;
        }
    }
}

static void shutdown_sidecar(int listen_fd)
{
    (void)close(listen_fd);
    sim_close();
    for (uint32_t i = 0U; i < g_ws_count; ++i) {
        (void)close(g_ws_clients[i].fd);
    }
    g_ws_count = 0U;
}

int main(int argc, char* argv[])
{
    init_cors_from_env();

    uint16_t port = parse_port(argc, argv);

    impairment_config_default(g_impairment);
    g_impairment.enabled = false;

    if (sim_init() != Result::OK) {
        (void)printf("chaos_lab_sidecar: sim_init failed\n");
        return 1;
    }

    int listen_fd = setup_listen_socket(port);
    if (listen_fd < 0) {
        sim_close();
        return 1;
    }

    (void)signal(SIGINT, signal_handler);

    (void)printf("chaos_lab_sidecar: listening on port %u\n", static_cast<unsigned int>(port));

    run_main_loop(listen_fd);
    shutdown_sidecar(listen_fd);

    return 0;
}
