#include "quic/connection/controler/connection_flow_control.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace quic {

ConnectionFlowControl::ConnectionFlowControl(StreamIDGenerator::StreamStarter starter):
    peer_control_send_data_size_(0),
    control_peer_send_data_size_(0),
    peer_control_max_bidirectional_stream_id_(0),
    peer_control_max_unidirectional_stream_id_(0),
    control_peer_max_bidirectional_stream_id_(0),
    control_peer_max_unidirectional_stream_id_(0),
    id_generator_(starter) {}

void ConnectionFlowControl::UpdateConfig(const TransportParam& tp) {
    peer_control_send_max_data_limit_ = tp.GetInitialMaxData();
    control_peer_send_max_data_limit_ = tp.GetInitialMaxData();
    peer_control_bidirectional_stream_limit_ = tp.GetInitialMaxStreamsBidi();
    peer_control_unidirectional_stream_limit_ = tp.GetInitialMaxStreamsUni();
    control_peer_bidirectional_stream_limit_ = tp.GetInitialMaxStreamsBidi();
    control_peer_unidirectional_stream_limit_ = tp.GetInitialMaxStreamsUni();
}

void ConnectionFlowControl::AddPeerControlSendData(uint32_t size) {
    peer_control_send_data_size_ += size;
}

void ConnectionFlowControl::AddPeerControlSendDataLimit(uint64_t limit) {
    if (peer_control_send_max_data_limit_ < limit) {
        peer_control_send_max_data_limit_ = limit;
    }
}

bool ConnectionFlowControl::CheckPeerControlSendDataLimit(
    uint64_t& can_send_size, std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (peer_control_send_data_size_ >= peer_control_send_max_data_limit_) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(peer_control_send_max_data_limit_);
        send_frame = frame;
        return false;
    }

    // TODO put 8912 to config
    can_send_size = peer_control_send_max_data_limit_ - peer_control_send_data_size_;
    if (peer_control_send_max_data_limit_ - peer_control_send_data_size_ < 8912) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(peer_control_send_max_data_limit_);
        send_frame = frame;
    }
    return true;
}

bool ConnectionFlowControl::AddControlPeerSendData(uint32_t size) {
    control_peer_send_data_size_ += size;
    return control_peer_send_data_size_ <= control_peer_send_max_data_limit_;
}

bool ConnectionFlowControl::CheckControlPeerSendDataLimit(std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (control_peer_send_data_size_ >= control_peer_send_max_data_limit_) {
        return false;
    }

    // check remote data limit. TODO put 8912 to config
    if (control_peer_send_max_data_limit_ - control_peer_send_data_size_ < 8912) {
        control_peer_send_max_data_limit_ += 8912;
        auto frame = std::make_shared<MaxDataFrame>();
        frame->SetMaximumData(control_peer_send_max_data_limit_);
        send_frame = frame;
    }
    return true;
}

void ConnectionFlowControl::AddPeerControlBidirectionStreamLimit(uint64_t limit) {
    if (peer_control_bidirectional_stream_limit_ < limit) {
        peer_control_bidirectional_stream_limit_ = limit;
    }

    // RFC 9000 Section 4.1: A sender MUST ignore any MAX_STREAM_DATA or MAX_DATA frames that do not increase flow
    // control limits.
}

bool ConnectionFlowControl::CheckPeerControlBidirectionStreamLimit(
    uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    // Peek at next stream ID without incrementing counter
    stream_id = id_generator_.PeekNextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);

    // Check if we've reached the upper limit of flow control
    if (stream_id >> 2 > peer_control_bidirectional_stream_limit_) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(peer_control_bidirectional_stream_limit_);
        send_frame = frame;
        return false;  // Don't increment counter on failure
    }

    // Check passed, now actually allocate the stream ID
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);

    // TODO put 4 to config
    if (peer_control_bidirectional_stream_limit_ - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(peer_control_bidirectional_stream_limit_);
        send_frame = frame;
    }
    peer_control_max_bidirectional_stream_id_ = stream_id;
    return true;
}

void ConnectionFlowControl::AddPeerControlUnidirectionStreamLimit(uint64_t limit) {
    if (peer_control_unidirectional_stream_limit_ < limit) {
        peer_control_unidirectional_stream_limit_ = limit;
    }
}

bool ConnectionFlowControl::CheckPeerControlUnidirectionStreamLimit(
    uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    // Peek at next stream ID without incrementing counter
    stream_id = id_generator_.PeekNextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);

    // Check if we've reached the upper limit of flow control
    if (stream_id >> 2 > peer_control_unidirectional_stream_limit_) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(peer_control_unidirectional_stream_limit_);
        send_frame = frame;
        return false;  // Don't increment counter on failure
    }

    // Check passed, now actually allocate the stream ID
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);

    // TODO put 4 to config
    if (peer_control_unidirectional_stream_limit_ - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(peer_control_unidirectional_stream_limit_);
        send_frame = frame;
    }
    peer_control_max_unidirectional_stream_id_ = stream_id;
    return true;
}

bool ConnectionFlowControl::CheckControlPeerStreamLimit(uint64_t id, std::shared_ptr<IFrame>& send_frame) {
    if (StreamIDGenerator::GetStreamDirection(id) == StreamIDGenerator::StreamDirection::kUnidirectional) {
        if (id > 0) {
            control_peer_max_unidirectional_stream_id_ = id;
        }
        return CheckControlPeerUnidirectionStreamLimit(send_frame);
    } else {
        if (id > 0) {
            control_peer_max_bidirectional_stream_id_ = id;
        }
        return CheckControlPeerBidirectionStreamLimit(send_frame);
    }
}

bool ConnectionFlowControl::CheckControlPeerBidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    uint64_t current_stream_count = control_peer_max_bidirectional_stream_id_ >> 2;

    if (control_peer_bidirectional_stream_limit_ < current_stream_count) {
        return false;
    }

    uint64_t remaining = control_peer_bidirectional_stream_limit_ - current_stream_count;

    // Proactive stream limit increase: when remaining streams < threshold, expand capacity
    // This reduces latency by avoiding STREAMS_BLOCKED -> MAX_STREAMS round trip
    if (remaining < 4) {
        uint64_t old_limit = control_peer_bidirectional_stream_limit_;
        control_peer_bidirectional_stream_limit_ += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsBidirectional);
        frame->SetMaximumStreams(control_peer_bidirectional_stream_limit_);
        send_frame = frame;

        common::LOG_DEBUG(
            "ConnectionFlowControl: Proactive bidirectional stream limit increase: %llu -> %llu (remaining was %llu)",
            old_limit, control_peer_bidirectional_stream_limit_, remaining);
    }
    return true;
}

bool ConnectionFlowControl::CheckControlPeerUnidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    uint64_t current_stream_count = control_peer_max_unidirectional_stream_id_ >> 2;

    if (control_peer_unidirectional_stream_limit_ < current_stream_count) {
        return false;
    }

    uint64_t remaining = control_peer_unidirectional_stream_limit_ - current_stream_count;

    // Proactive stream limit increase: when remaining streams < threshold, expand capacity
    if (remaining < 4) {
        uint64_t old_limit = control_peer_unidirectional_stream_limit_;
        control_peer_unidirectional_stream_limit_ += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsUnidirectional);
        frame->SetMaximumStreams(control_peer_unidirectional_stream_limit_);
        send_frame = frame;

        common::LOG_DEBUG(
            "ConnectionFlowControl: Proactive unidirectional stream limit increase: %llu -> %llu (remaining was %llu)",
            old_limit, control_peer_unidirectional_stream_limit_, remaining);
    }
    return true;
}

}  // namespace quic
}  // namespace quicx
