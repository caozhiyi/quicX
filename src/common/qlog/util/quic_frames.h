// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_UTIL_QUIC_FRAMES
#define COMMON_QLOG_UTIL_QUIC_FRAMES

#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "common/qlog/util/qlog_types.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/if_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/retire_connection_id_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace common {

/**
 * @brief Convert raw bytes to lowercase hex string.
 *
 * Used to serialize CIDs, tokens and stateless reset tokens for qlog.
 */
inline std::string BytesToHex(const uint8_t* data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

/**
 * @brief Reconstruct ACK ranges from RFC 9000 §19.3 wire-format encoding.
 *
 * The wire format encodes a Largest Acknowledged + First ACK Range, followed
 * by a list of (Gap, ACK Range Length) pairs. qlog requires explicit
 * [smallest, largest] pairs.
 */
inline std::string SerializeAckRanges(quic::AckFrame* ack) {
    std::ostringstream oss;
    oss << "[";

    uint64_t largest = ack->GetLargestAck();
    uint64_t first_range = ack->GetFirstAckRange();
    // First range covers [largest - first_range, largest].
    uint64_t smallest_first = (largest >= first_range) ? (largest - first_range) : 0;
    oss << "[" << smallest_first << "," << largest << "]";

    // Subsequent ranges
    uint64_t cursor = smallest_first;  // current "smallest of last reported range"
    bool first = false;
    (void)first;
    const auto& ranges = ack->GetAckRange();
    for (const auto& r : ranges) {
        // RFC 9000: the next "Largest" = cursor - Gap - 2.
        // Each ACK Range Length covers [Largest - Length, Largest].
        uint64_t gap = r.GetGap();
        uint64_t length = r.GetAckRangeLength();
        if (cursor < gap + 2) {
            break;  // malformed, stop emitting
        }
        uint64_t this_largest = cursor - gap - 2;
        uint64_t this_smallest = (this_largest >= length) ? (this_largest - length) : 0;
        oss << ",[" << this_smallest << "," << this_largest << "]";
        cursor = this_smallest;
    }
    oss << "]";
    return oss.str();
}

/**
 * @brief Serialize a single QUIC frame to the qlog frame object form.
 *
 * Output is a JSON object that always includes "frame_type" plus the
 * type-specific fields required by draft-ietf-quic-qlog-quic-events.
 *
 * The frame is passed by raw pointer (non-owning) since callers usually
 * already hold a shared_ptr; we only need read access.
 */
inline std::string FrameToJson(quic::IFrame* frame) {
    if (!frame) {
        return "{\"frame_type\":\"unknown\"}";
    }

    const uint16_t type = frame->GetType();
    const char* type_str = FrameTypeToQlogString(static_cast<quic::FrameType>(type));

    std::ostringstream oss;
    oss << "{\"frame_type\":\"" << type_str << "\"";

    // STREAM (0x08-0x0f) ---------------------------------------------------
    if (type >= quic::FrameType::kStream && type <= 0x0f) {
        if (auto* sf = dynamic_cast<quic::StreamFrame*>(frame)) {
            oss << ",\"stream_id\":" << sf->GetStreamID();
            oss << ",\"offset\":" << (sf->HasOffset() ? sf->GetOffset() : 0);
            oss << ",\"length\":" << sf->GetLength();
            oss << ",\"fin\":" << (sf->IsFin() ? "true" : "false");
        }
        oss << "}";
        return oss.str();
    }

    switch (static_cast<quic::FrameType>(type)) {
        case quic::FrameType::kAck:
        case quic::FrameType::kAckEcn: {
            if (auto* af = dynamic_cast<quic::AckFrame*>(frame)) {
                oss << ",\"ack_delay\":" << af->GetAckDelay();
                oss << ",\"acked_ranges\":" << SerializeAckRanges(af);
                if (auto* ecn = dynamic_cast<quic::AckEcnFrame*>(frame)) {
                    oss << ",\"ect0\":" << ecn->GetEct0();
                    oss << ",\"ect1\":" << ecn->GetEct1();
                    oss << ",\"ce\":" << ecn->GetEcnCe();
                }
            }
            break;
        }
        case quic::FrameType::kCrypto: {
            if (auto* cf = dynamic_cast<quic::CryptoFrame*>(frame)) {
                oss << ",\"offset\":" << cf->GetOffset();
                oss << ",\"length\":" << cf->GetLength();
            }
            break;
        }
        case quic::FrameType::kResetStream: {
            if (auto* rf = dynamic_cast<quic::ResetStreamFrame*>(frame)) {
                oss << ",\"stream_id\":" << rf->GetStreamID();
                oss << ",\"error_code\":" << rf->GetAppErrorCode();
                oss << ",\"final_size\":" << rf->GetFinalSize();
            }
            break;
        }
        case quic::FrameType::kStopSending: {
            if (auto* sf = dynamic_cast<quic::StopSendingFrame*>(frame)) {
                oss << ",\"stream_id\":" << sf->GetStreamID();
                oss << ",\"error_code\":" << sf->GetAppErrorCode();
            }
            break;
        }
        case quic::FrameType::kMaxData: {
            if (auto* mf = dynamic_cast<quic::MaxDataFrame*>(frame)) {
                oss << ",\"maximum\":" << mf->GetMaximumData();
            }
            break;
        }
        case quic::FrameType::kMaxStreamData: {
            if (auto* mf = dynamic_cast<quic::MaxStreamDataFrame*>(frame)) {
                oss << ",\"stream_id\":" << mf->GetStreamID();
                oss << ",\"maximum\":" << mf->GetMaximumData();
            }
            break;
        }
        case quic::FrameType::kMaxStreamsBidirectional:
        case quic::FrameType::kMaxStreamsUnidirectional: {
            if (auto* mf = dynamic_cast<quic::MaxStreamsFrame*>(frame)) {
                oss << ",\"stream_type\":\""
                    << (type == quic::FrameType::kMaxStreamsBidirectional ? "bidirectional" : "unidirectional")
                    << "\"";
                oss << ",\"maximum\":" << mf->GetMaximumStreams();
            }
            break;
        }
        case quic::FrameType::kDataBlocked: {
            if (auto* df = dynamic_cast<quic::DataBlockedFrame*>(frame)) {
                oss << ",\"limit\":" << df->GetMaximumData();
            }
            break;
        }
        case quic::FrameType::kStreamDataBlocked: {
            if (auto* df = dynamic_cast<quic::StreamDataBlockedFrame*>(frame)) {
                oss << ",\"stream_id\":" << df->GetStreamID();
                oss << ",\"limit\":" << df->GetMaximumData();
            }
            break;
        }
        case quic::FrameType::kStreamsBlockedBidirectional:
        case quic::FrameType::kStreamsBlockedUnidirectional: {
            if (auto* sf = dynamic_cast<quic::StreamsBlockedFrame*>(frame)) {
                oss << ",\"stream_type\":\""
                    << (type == quic::FrameType::kStreamsBlockedBidirectional ? "bidirectional" : "unidirectional")
                    << "\"";
                oss << ",\"limit\":" << sf->GetMaximumStreams();
            }
            break;
        }
        case quic::FrameType::kNewConnectionId: {
            if (auto* nf = dynamic_cast<quic::NewConnectionIDFrame*>(frame)) {
                oss << ",\"sequence_number\":" << nf->GetSequenceNumber();
                oss << ",\"retire_prior_to\":" << nf->GetRetirePriorTo();
                quic::ConnectionID cid;
                nf->GetConnectionID(cid);
                oss << ",\"connection_id\":\"" << BytesToHex(cid.GetID(), cid.GetLength()) << "\"";
                oss << ",\"connection_id_length\":" << static_cast<int>(cid.GetLength());
                oss << ",\"stateless_reset_token\":\""
                    << BytesToHex(nf->GetStatelessResetToken(), 16) << "\"";
            }
            break;
        }
        case quic::FrameType::kRetireConnectionId: {
            if (auto* rf = dynamic_cast<quic::RetireConnectionIDFrame*>(frame)) {
                oss << ",\"sequence_number\":" << rf->GetSequenceNumber();
            }
            break;
        }
        case quic::FrameType::kPathChallenge: {
            if (auto* pf = dynamic_cast<quic::PathChallengeFrame*>(frame)) {
                oss << ",\"data\":\"" << BytesToHex(pf->GetData(), 8) << "\"";
            }
            break;
        }
        case quic::FrameType::kPathResponse: {
            if (auto* pf = dynamic_cast<quic::PathResponseFrame*>(frame)) {
                oss << ",\"data\":\"" << BytesToHex(pf->GetData(), 8) << "\"";
            }
            break;
        }
        case quic::FrameType::kConnectionClose:
        case quic::FrameType::kConnectionCloseApp: {
            if (auto* cf = dynamic_cast<quic::ConnectionCloseFrame*>(frame)) {
                oss << ",\"error_space\":\""
                    << (type == quic::FrameType::kConnectionClose ? "transport" : "application") << "\"";
                oss << ",\"error_code\":" << cf->GetErrorCode();
                oss << ",\"raw_error_code\":" << cf->GetErrorCode();
                if (type == quic::FrameType::kConnectionClose) {
                    oss << ",\"trigger_frame_type\":" << cf->GetErrFrameType();
                }
                // Reason is plain text; escape via simple replace.
                std::string reason = cf->GetReason();
                std::string escaped;
                escaped.reserve(reason.size());
                for (char c : reason) {
                    if (c == '\\' || c == '"') {
                        escaped.push_back('\\');
                        escaped.push_back(c);
                    } else if (c == '\n') {
                        escaped += "\\n";
                    } else if (c == '\r') {
                        escaped += "\\r";
                    } else if (c == '\t') {
                        escaped += "\\t";
                    } else if (static_cast<unsigned char>(c) < 0x20) {
                        // skip other control chars
                    } else {
                        escaped.push_back(c);
                    }
                }
                oss << ",\"reason\":\"" << escaped << "\"";
            }
            break;
        }
        case quic::FrameType::kNewToken: {
            if (auto* nt = dynamic_cast<quic::NewTokenFrame*>(frame)) {
                oss << ",\"token\":{\"type\":\"resumption\",\"data\":\""
                    << BytesToHex(nt->GetToken(), nt->GetTokenLength()) << "\"}";
            }
            break;
        }
        case quic::FrameType::kPadding:
        case quic::FrameType::kPing:
        case quic::FrameType::kHandshakeDone:
        default:
            // Frames with no extra fields beyond frame_type.
            break;
    }

    oss << "}";
    return oss.str();
}

inline std::string FrameToJson(const std::shared_ptr<quic::IFrame>& frame) {
    return FrameToJson(frame.get());
}

}  // namespace common
}  // namespace quicx

#endif
