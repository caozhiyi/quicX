#include "common/log/log.h"

#include "quic/connection/connection_closer.h"
#include "quic/connection/connection_crypto.h"
#include "quic/connection/connection_frame_processor.h"
#include "quic/connection/connection_id_coordinator.h"
#include "quic/connection/connection_path_manager.h"
#include "quic/connection/connection_state_machine.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/connection_flow_control.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/error.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/util.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/retire_connection_id_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/stream_id_generator.h"

namespace quicx {
namespace quic {

FrameProcessor::FrameProcessor(ConnectionStateMachine& state_machine, ConnectionCrypto& connection_crypto,
    ConnectionFlowControl& flow_control, SendManager& send_manager, StreamManager& stream_manager,
    ConnectionIDCoordinator& cid_coordinator, PathManager& path_manager, ConnectionCloser& connection_closer,
    TransportParam& transport_param, std::string& token):
    state_machine_(state_machine),
    connection_crypto_(connection_crypto),
    flow_control_(flow_control),
    send_manager_(send_manager),
    stream_manager_(stream_manager),
    cid_coordinator_(cid_coordinator),
    path_manager_(path_manager),
    connection_closer_(connection_closer),
    transport_param_(transport_param),
    token_(token) {}

// ==================== Frame Dispatching ====================

bool FrameProcessor::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level) {
    for (size_t i = 0; i < frames.size(); i++) {
        auto type = frames[i]->GetType();
        switch (type) {
            // ********** control frame **********
            case FrameType::kPadding:
            case FrameType::kPing:
                // Just update timeout, no additional processing
                break;
            case FrameType::kAck:
            case FrameType::kAckEcn:
                if (!OnAckFrame(frames[i], crypto_level)) {
                    return false;
                }
                break;
            case FrameType::kCrypto:
                if (!OnCryptoFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kNewToken:
                if (!OnNewTokenFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kMaxData:
                if (!OnMaxDataFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kDataBlocked:
                if (!OnDataBlockFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kStreamsBlockedBidirectional:
            case FrameType::kStreamsBlockedUnidirectional:
                if (!OnStreamBlockFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kMaxStreamsBidirectional:
            case FrameType::kMaxStreamsUnidirectional:
                if (!OnMaxStreamFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kNewConnectionId:
                if (!OnNewConnectionIDFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kRetireConnectionId:
                if (!OnRetireConnectionIDFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kPathChallenge:
                if (!OnPathChallengeFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kPathResponse:
                if (!OnPathResponseFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kConnectionClose:
                if (!OnConnectionCloseFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kConnectionCloseApp:
                if (!OnConnectionCloseAppFrame(frames[i])) {
                    return false;
                }
                break;
            case FrameType::kHandshakeDone:
                if (handshake_done_cb_ && !handshake_done_cb_(frames[i])) {
                    return false;
                }
                break;
            // ********** stream frame **********
            case FrameType::kResetStream:
            case FrameType::kStopSending:
            case FrameType::kStreamDataBlocked:
            case FrameType::kMaxStreamData:
                if (!OnStreamFrame(frames[i])) {
                    return false;
                }
                break;
            default:
                if (StreamFrame::IsStreamFrame(type)) {
                    if (!OnStreamFrame(frames[i])) {
                        return false;
                    }
                } else {
                    common::LOG_ERROR("invalid frame type. type:%s", type);
                    return false;
                }
        }
    }
    return true;
}

// ==================== Frame Handlers ====================

bool FrameProcessor::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<IStreamFrame>(frame);
    if (!stream_frame) {
        common::LOG_ERROR("invalid stream frame.");
        return false;
    }

    // Allow processing of application data only when encryption is ready; 0-RTT stream frames
    // arrive before handshake confirmation and must be accepted per RFC (subject to anti-replay policy
    // which is handled at TLS/session level). Here we don't gate on connection state.
    common::LOG_DEBUG("process stream data frame. stream id:%d", stream_frame->GetStreamID());
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream_ptr = stream_manager_.FindStream(stream_id);
    if (stream_ptr) {
        // CRITICAL: Hold a local shared_ptr to prevent use-after-free
        // If the callback calls error_handler_ which removes the stream,
        // this local copy keeps the object alive until we return
        flow_control_.AddControlPeerSendData(stream_ptr->OnFrame(frame));
        return true;
    }

    // check streams limit
    std::shared_ptr<IFrame> send_frame;
    bool can_make_stream = flow_control_.CheckControlPeerStreamLimit(stream_id, send_frame);
    if (send_frame && to_send_frame_cb_) {
        to_send_frame_cb_(send_frame);
    }

    if (!can_make_stream) {
        // RFC 9000 Section 4.6: An endpoint that receives a frame with a
        // stream ID exceeding the limit it has sent treat this as a connection error of type STREAM_LIMIT_ERROR
        if (inner_connection_close_cb_) {
            inner_connection_close_cb_(QuicErrorCode::kStreamLimitError, frame->GetType(), "stream limit error.");
        }
        common::LOG_ERROR("stream limit error. stream id:%d", stream_id);
        return false;
    }

    // Create remote stream (delegated to stream manager)
    StreamDirection direction;
    uint32_t init_size;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::StreamDirection::kBidirectional) {
        direction = StreamDirection::kBidi;
        init_size = transport_param_.GetInitialMaxStreamDataBidiRemote();
    } else {
        direction = StreamDirection::kRecv;
        init_size = transport_param_.GetInitialMaxStreamDataUni();
    }

    auto new_stream = stream_manager_.CreateRemoteStream(init_size, stream_id, direction);
    if (!new_stream) {
        common::LOG_ERROR("Failed to create remote stream %llu", stream_id);
        return false;
    }

    // check peer data limit
    // RFC 9000 Section 4.1: A receiver MUST close the connection with an error of type FLOW_CONTROL_ERROR if the
    // sender violates the advertised connection or stream data limits
    if (!flow_control_.CheckControlPeerSendDataLimit(send_frame)) {
        if (inner_connection_close_cb_) {
            inner_connection_close_cb_(
                QuicErrorCode::kFlowControlError, frame->GetType(), "flow control stream data limit.");
        }
        return false;
    }
    if (send_frame && to_send_frame_cb_) {
        to_send_frame_cb_(send_frame);
    }
    // notify stream state
    if (stream_state_cb_) {
        stream_state_cb_(new_stream, 0);
    }
    // new stream process frame
    flow_control_.AddControlPeerSendData(new_stream->OnFrame(frame));
    return true;
}

bool FrameProcessor::OnAckFrame(std::shared_ptr<IFrame> frame, uint16_t crypto_level) {
    auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
    send_manager_.OnPacketAck(ns, frame);
    return true;
}

bool FrameProcessor::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    connection_crypto_.OnCryptoFrame(frame);
    return true;
}

bool FrameProcessor::OnNewTokenFrame(std::shared_ptr<IFrame> frame) {
    auto token_frame = std::dynamic_pointer_cast<NewTokenFrame>(frame);
    if (!token_frame) {
        common::LOG_ERROR("invalid new token frame.");
        return false;
    }
    auto data = token_frame->GetToken();
    token_ = std::move(std::string((const char*)data, token_frame->GetTokenLength()));
    return true;
}

bool FrameProcessor::OnMaxDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxDataFrame>(frame);
    if (!max_data_frame) {
        common::LOG_ERROR("invalid max data frame.");
        return false;
    }
    uint64_t max_data_size = max_data_frame->GetMaximumData();
    flow_control_.AddPeerControlSendDataLimit(max_data_size);
    return true;
}

bool FrameProcessor::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckControlPeerSendDataLimit(send_frame);
    if (send_frame && to_send_frame_cb_) {
        to_send_frame_cb_(send_frame);
    }
    return true;
}

bool FrameProcessor::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckControlPeerStreamLimit(0, send_frame);
    if (send_frame && to_send_frame_cb_) {
        to_send_frame_cb_(send_frame);
        if (active_send_cb_) {
            active_send_cb_();  // Immediately send MAX_STREAMS to reduce latency
        }

        common::LOG_INFO("Received STREAMS_BLOCKED, sending MAX_STREAMS immediately");
    }
    return true;
}

bool FrameProcessor::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);

    uint64_t old_limit = 0;
    uint64_t new_limit = stream_block_frame->GetMaximumStreams();

    if (stream_block_frame->GetType() == FrameType::kMaxStreamsBidirectional) {
        old_limit = flow_control_.GetPeerControlBidirectionStreamLimit();
        flow_control_.AddPeerControlBidirectionStreamLimit(new_limit);

        common::LOG_INFO(
            "Received MAX_STREAMS_BIDIRECTIONAL: %llu -> %llu, retrying pending requests", old_limit, new_limit);
    } else {
        old_limit = flow_control_.GetPeerControlUnidirectionStreamLimit();
        flow_control_.AddPeerControlUnidirectionStreamLimit(new_limit);

        common::LOG_INFO(
            "Received MAX_STREAMS_UNIDIRECTIONAL: %llu -> %llu, retrying pending requests", old_limit, new_limit);
    }

    // Trigger retry of pending stream creation requests
    if (retry_pending_stream_requests_cb_) {
        retry_pending_stream_requests_cb_();
    }

    return true;
}

bool FrameProcessor::OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto new_cid_frame = std::dynamic_pointer_cast<NewConnectionIDFrame>(frame);
    if (!new_cid_frame) {
        common::LOG_ERROR("invalid new connection id frame.");
        return false;
    }

    // If Retire Prior To > 0, we need to retire old CIDs and send RETIRE_CONNECTION_ID
    uint64_t retire_prior_to = new_cid_frame->GetRetirePriorTo();
    if (retire_prior_to > 0) {
        // Send RETIRE_CONNECTION_ID for all CIDs with sequence < retire_prior_to
        // We need to iterate from 0 to retire_prior_to-1
        for (uint64_t seq = 0; seq < retire_prior_to; ++seq) {
            auto retire = std::make_shared<RetireConnectionIDFrame>();
            retire->SetSequenceNumber(seq);
            if (to_send_frame_cb_) {
                to_send_frame_cb_(retire);
            }
        }
        // Remove these CIDs from our remote pool
        cid_coordinator_.GetRemoteConnectionIDManager()->RetireIDBySequence(retire_prior_to - 1);
    }

    // Add new CID to pool
    ConnectionID id;
    new_cid_frame->GetConnectionID(id);
    cid_coordinator_.GetRemoteConnectionIDManager()->AddID(id);
    return true;
}

bool FrameProcessor::OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto retire_cid_frame = std::dynamic_pointer_cast<RetireConnectionIDFrame>(frame);
    if (!retire_cid_frame) {
        common::LOG_ERROR("invalid retire connection id frame.");
        return false;
    }
    // Peer is retiring a CID we provided to them, remove from local pool
    cid_coordinator_.GetLocalConnectionIDManager()->RetireIDBySequence(retire_cid_frame->GetSequenceNumber());
    return true;
}

bool FrameProcessor::OnConnectionCloseFrame(std::shared_ptr<IFrame> frame) {
    auto close_frame = std::dynamic_pointer_cast<ConnectionCloseFrame>(frame);
    if (!close_frame) {
        common::LOG_ERROR("invalid connection close frame.");
        return false;
    }

    if (state_machine_.GetState() != ConnectionStateType::kStateConnected &&
        state_machine_.GetState() != ConnectionStateType::kStateClosing) {
        return false;
    }

    // Cancel graceful close if it's pending (peer initiated close)
    connection_closer_.CancelGracefulClose();

    // Store error info from peer's CONNECTION_CLOSE for application notification
    connection_closer_.StorePeerCloseInfo(
        close_frame->GetErrorCode(), close_frame->GetErrFrameType(), close_frame->GetReason());

    // Received CONNECTION_CLOSE from peer: enter Draining state
    // RFC 9000: In draining state, endpoint MUST NOT send any packets
    state_machine_.OnConnectionCloseFrameReceived();

    common::LOG_INFO("Connection entering draining state. error_code:%u, reason:%s", close_frame->GetErrorCode(),
        close_frame->GetReason().c_str());

    return true;
}

bool FrameProcessor::OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame) {
    return OnConnectionCloseFrame(frame);
}

bool FrameProcessor::OnPathChallengeFrame(std::shared_ptr<IFrame> frame) {
    auto challenge_frame = std::dynamic_pointer_cast<PathChallengeFrame>(frame);
    if (!challenge_frame) {
        common::LOG_ERROR("invalid path challenge frame.");
        return false;
    }
    auto data = challenge_frame->GetData();
    std::shared_ptr<IFrame> response_frame;
    path_manager_.OnPathChallenge(data, response_frame);
    if (response_frame && to_send_frame_cb_) {
        to_send_frame_cb_(response_frame);
    }
    return true;
}

bool FrameProcessor::OnPathResponseFrame(std::shared_ptr<IFrame> frame) {
    auto response_frame = std::dynamic_pointer_cast<PathResponseFrame>(frame);
    if (!response_frame) {
        common::LOG_ERROR("invalid path response frame.");
        return false;
    }
    auto data = response_frame->GetData();
    path_manager_.OnPathResponse(data);
    return true;
}

}  // namespace quic
}  // namespace quicx
