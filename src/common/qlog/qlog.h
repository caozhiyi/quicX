// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

/**
 * @file qlog.h
 * @brief Unified public interface for qlog system
 *
 * This is the only header file that external modules need to include
 */

#ifndef COMMON_QLOG_QLOG
#define COMMON_QLOG_QLOG

#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/event/http3_events.h"
#include "common/qlog/event/qlog_event.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/security_events.h"
#include "common/qlog/event/transport_events.h"

#include "common/qlog/qlog_config.h"
#include "common/qlog/qlog_manager.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/util/qlog_constants.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

// ========== Convenience macro definitions ==========

/**
 * @brief Conditional compilation switch
 *
 * Controlled by CMake option:
 * cmake -DQUICX_ENABLE_QLOG=ON/OFF
 */
#ifdef QUICX_ENABLE_QLOG
#define QLOG_ENABLED 1
#else
#define QLOG_ENABLED 0
#endif

#if QLOG_ENABLED

/**
 * @brief Get current timestamp (microseconds)
 */
#define QLOG_TIME_US() (::quicx::common::UTCTimeMsec() * 1000)

/**
 * @brief Log generic event
 *
 * Usage example:
 * QLOG_EVENT(trace, "quic:packet_sent",
 *            std::make_unique<PacketSentData>(...));
 */
#define QLOG_EVENT(trace, event_name, event_data)                    \
    do {                                                             \
        if (trace) {                                                 \
            trace->LogEvent(QLOG_TIME_US(), event_name, event_data); \
        }                                                            \
    } while (0)

/**
 * @brief Log packet_sent event
 *
 * Usage example:
 * common::PacketSentData data;
 * data.packet_number = 123;
 * ...
 * QLOG_PACKET_SENT(trace, data);
 */
#define QLOG_PACKET_SENT(trace, data)                   \
    do {                                                \
        if (trace) {                                    \
            trace->LogPacketSent(QLOG_TIME_US(), data); \
        }                                               \
    } while (0)

/**
 * @brief Log packet_received event
 */
#define QLOG_PACKET_RECEIVED(trace, data)                   \
    do {                                                    \
        if (trace) {                                        \
            trace->LogPacketReceived(QLOG_TIME_US(), data); \
        }                                                   \
    } while (0)

/**
 * @brief Log recovery_metrics_updated event
 */
#define QLOG_METRICS_UPDATED(trace, data)                   \
    do {                                                    \
        if (trace) {                                        \
            trace->LogMetricsUpdated(QLOG_TIME_US(), data); \
        }                                                   \
    } while (0)

/**
 * @brief Log connection_started event
 */
#define QLOG_CONNECTION_STARTED(trace, data)                   \
    do {                                                       \
        if (trace) {                                           \
            trace->LogConnectionStarted(QLOG_TIME_US(), data); \
        }                                                      \
    } while (0)

/**
 * @brief Log connection_closed event
 */
#define QLOG_CONNECTION_CLOSED(trace, data)                   \
    do {                                                      \
        if (trace) {                                          \
            trace->LogConnectionClosed(QLOG_TIME_US(), data); \
        }                                                     \
    } while (0)

/**
 * @brief Log packet_lost event
 */
#define QLOG_PACKET_LOST(trace, data)                                                                         \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::PacketLostData>(data);                        \
            trace->LogEvent(QLOG_TIME_US(), ::quicx::common::QlogEvents::kPacketLost, std::move(event_data)); \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log congestion_state_updated event
 */
#define QLOG_CONGESTION_STATE_UPDATED(trace, data)                                                            \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::CongestionStateUpdatedData>(data);            \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kCongestionStateUpdated, std::move(event_data)); \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log stream_state_updated event
 */
#define QLOG_STREAM_STATE_UPDATED(trace, data)                                                                \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::StreamStateUpdatedData>(data);                \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kStreamStateUpdated, std::move(event_data));     \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log packet_dropped event
 */
#define QLOG_PACKET_DROPPED(trace, data)                                                                      \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::PacketDroppedData>(data);                     \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kPacketDropped, std::move(event_data));          \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log packet_buffered event
 */
#define QLOG_PACKET_BUFFERED(trace, data)                                                                     \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::PacketBufferedData>(data);                    \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kPacketBuffered, std::move(event_data));         \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log connection_id_updated event
 */
#define QLOG_CONNECTION_ID_UPDATED(trace, data)                                                               \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::ConnectionIdUpdatedData>(data);               \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kConnectionIdUpdated, std::move(event_data));    \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log security:key_updated event
 */
#define QLOG_KEY_UPDATED(trace, data)                                                                         \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::KeyUpdatedData>(data);                        \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kKeyUpdated, std::move(event_data));             \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log security:key_discarded event
 */
#define QLOG_KEY_DISCARDED(trace, data)                                                                       \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::KeyDiscardedData>(data);                      \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kKeyDiscarded, std::move(event_data));           \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log recovery:marked_for_retransmit event
 */
#define QLOG_MARKED_FOR_RETRANSMIT(trace, data)                                                               \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::MarkedForRetransmitData>(data);               \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kMarkedForRetransmit, std::move(event_data));    \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log http3:frame_created event
 */
#define QLOG_HTTP3_FRAME_CREATED(trace, data)                                                                 \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::Http3FrameCreatedData>(data);                 \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kHttp3FrameCreated, std::move(event_data));      \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log http3:frame_parsed event
 */
#define QLOG_HTTP3_FRAME_PARSED(trace, data)                                                                  \
    do {                                                                                                      \
        if (trace) {                                                                                          \
            auto event_data = std::make_unique<::quicx::common::Http3FrameParsedData>(data);                  \
            trace->LogEvent(                                                                                  \
                QLOG_TIME_US(), ::quicx::common::QlogEvents::kHttp3FrameParsed, std::move(event_data));       \
        }                                                                                                     \
    } while (0)

/**
 * @brief Log server_listening event
 */
#define QLOG_SERVER_LISTENING(manager, data)                                                                  \
    do {                                                                                                      \
        if ((manager).IsEnabled()) {                                                                          \
            auto trace = (manager).CreateTrace("server", ::quicx::common::VantagePoint::kServer);             \
            if (trace) {                                                                                      \
                auto event_data = std::make_unique<::quicx::common::ServerListeningData>(data);               \
                trace->LogEvent(                                                                              \
                    QLOG_TIME_US(), ::quicx::common::QlogEvents::kServerListening, std::move(event_data));    \
            }                                                                                                 \
        }                                                                                                     \
    } while (0)

#define QLOG_FLUSH(trace)                                                     \
    do {                                                                      \
        if (trace) {                                                          \
            (trace)->Flush();                                                 \
        }                                                                     \
    } while (0)

#else  // QLOG_ENABLED == 0

// When qlog is disabled, macros expand to no-ops (zero overhead)
#define QLOG_TIME_US() 0
#define QLOG_EVENT(trace, event_name, event_data) ((void)0)
#define QLOG_PACKET_SENT(trace, data) ((void)(data))
#define QLOG_PACKET_RECEIVED(trace, data) ((void)(data))
#define QLOG_METRICS_UPDATED(trace, data) ((void)(data))
#define QLOG_CONNECTION_STARTED(trace, data) ((void)(data))
#define QLOG_CONNECTION_CLOSED(trace, data) ((void)(data))
#define QLOG_PACKET_LOST(trace, data) ((void)(data))
#define QLOG_CONGESTION_STATE_UPDATED(trace, data) ((void)(data))
#define QLOG_STREAM_STATE_UPDATED(trace, data) ((void)(data))
#define QLOG_PACKET_DROPPED(trace, data) ((void)(data))
#define QLOG_PACKET_BUFFERED(trace, data) ((void)(data))
#define QLOG_CONNECTION_ID_UPDATED(trace, data) ((void)(data))
#define QLOG_KEY_UPDATED(trace, data) ((void)(data))
#define QLOG_KEY_DISCARDED(trace, data) ((void)(data))
#define QLOG_MARKED_FOR_RETRANSMIT(trace, data) ((void)(data))
#define QLOG_HTTP3_FRAME_CREATED(trace, data) ((void)(data))
#define QLOG_HTTP3_FRAME_PARSED(trace, data) ((void)(data))
#define QLOG_SERVER_LISTENING(manager, data) ((void)(data))
#define QLOG_FLUSH(trace) ((void)0)

#endif  // QLOG_ENABLED

}  // namespace common
}  // namespace quicx

#endif  // COMMON_QLOG_QLOG
