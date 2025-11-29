#include "quic/connection/controler/flow_control.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/streams_blocked_frame.h"

namespace quicx {
namespace quic {

FlowControl::FlowControl(StreamIDGenerator::StreamStarter starter):
    local_send_data_size_(0),
    remote_send_data_size_(0),
    local_max_bidirectional_stream_id_(0),
    local_max_unidirectional_stream_id_(0),
    remote_max_bidirectional_stream_id_(0),
    remote_max_unidirectional_stream_id_(0),
    id_generator_(starter) {}

void FlowControl::UpdateConfig(const TransportParam& tp) {
    local_send_max_data_limit_ = tp.GetInitialMaxData();
    remote_send_max_data_limit_ = tp.GetInitialMaxData();
    local_bidirectional_stream_limit_ = tp.GetInitialMaxStreamsBidi();
    local_unidirectional_stream_limit_ = tp.GetInitialMaxStreamsUni();
    remote_bidirectional_stream_limit_ = tp.GetInitialMaxStreamsBidi();
    remote_unidirectional_stream_limit_ = tp.GetInitialMaxStreamsUni();
}

void FlowControl::AddLocalSendData(uint32_t size) {
    local_send_data_size_ += size;
}

void FlowControl::AddLocalSendDataLimit(uint64_t limit) {
    if (local_send_max_data_limit_ < limit) {
        local_send_max_data_limit_ = limit;
    }
}

bool FlowControl::CheckLocalSendDataLimit(uint64_t& can_send_size, std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (local_send_data_size_ >= local_send_max_data_limit_) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(local_send_max_data_limit_);
        send_frame = frame;
        return false;
    }

    // TODO put 8912 to config
    can_send_size = local_send_max_data_limit_ - local_send_data_size_;
    if (local_send_max_data_limit_ - local_send_data_size_ < 8912) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(local_send_max_data_limit_);
        send_frame = frame;
    }
    return true;
}

void FlowControl::AddRemoteSendData(uint32_t size) {
    remote_send_data_size_ += size;
}

bool FlowControl::CheckRemoteSendDataLimit(std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (remote_send_data_size_ >= remote_send_max_data_limit_) {
        return false;
    }

    // check remote data limit. TODO put 8912 to config
    if (remote_send_max_data_limit_ - remote_send_data_size_ < 8912) {
        remote_send_max_data_limit_ += 8912;
        auto frame = std::make_shared<MaxDataFrame>();
        frame->SetMaximumData(remote_send_max_data_limit_);
        send_frame = frame;
    }
    return true;
}

void FlowControl::AddLocalBidirectionStreamLimit(uint64_t limit) {
    if (local_bidirectional_stream_limit_ < limit) {
        local_bidirectional_stream_limit_ = limit;
    }
}

bool FlowControl::CheckLocalBidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kBidirectional);
    // reaching the upper limit of flow control
    if (stream_id >> 2 > local_bidirectional_stream_limit_) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(local_bidirectional_stream_limit_);
        send_frame = frame;
        return false;
    }

    // TODO put 4 to config
    if (local_bidirectional_stream_limit_ - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedBidirectional);
        frame->SetMaximumStreams(local_bidirectional_stream_limit_);
        send_frame = frame;
    }
    local_max_bidirectional_stream_id_ = stream_id;
    return true;
}

void FlowControl::AddLocalUnidirectionStreamLimit(uint64_t limit) {
    if (local_unidirectional_stream_limit_ < limit) {
        local_unidirectional_stream_limit_ = limit;
    }
}

bool FlowControl::CheckLocalUnidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    stream_id = id_generator_.NextStreamID(StreamIDGenerator::StreamDirection::kUnidirectional);
    // reaching the upper limit of flow control
    if (stream_id >> 2 > local_unidirectional_stream_limit_) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(local_unidirectional_stream_limit_);
        send_frame = frame;
        return false;
    }

    // TODO put 4 to config
    if (local_unidirectional_stream_limit_ - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FrameType::kStreamsBlockedUnidirectional);
        frame->SetMaximumStreams(local_unidirectional_stream_limit_);
        send_frame = frame;
    }
    local_max_unidirectional_stream_id_ = stream_id;
    return true;
}

bool FlowControl::CheckRemoteStreamLimit(uint64_t id, std::shared_ptr<IFrame>& send_frame) {
    if (StreamIDGenerator::GetStreamDirection(id) == StreamIDGenerator::StreamDirection::kUnidirectional) {
        if (id > 0) {
            remote_max_unidirectional_stream_id_ = id;
        }
        return CheckRemoteUnidirectionStreamLimit(send_frame);
    } else {
        if (id > 0) {
            remote_max_bidirectional_stream_id_ = id;
        }
        return CheckRemoteBidirectionStreamLimit(send_frame);
    }
}

bool FlowControl::CheckRemoteBidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    if (remote_bidirectional_stream_limit_ < remote_max_bidirectional_stream_id_ >> 2) {
        return false;
    }

    if (remote_bidirectional_stream_limit_ - (remote_max_bidirectional_stream_id_ >> 2) < 4) {
        remote_bidirectional_stream_limit_ += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsBidirectional);
        frame->SetMaximumStreams(remote_bidirectional_stream_limit_);
        send_frame = frame;
    }
    return true;
}

bool FlowControl::CheckRemoteUnidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    if (remote_unidirectional_stream_limit_ < remote_max_unidirectional_stream_id_ >> 2) {
        return false;
    }

    if (remote_unidirectional_stream_limit_ - (remote_max_unidirectional_stream_id_ >> 2) < 4) {
        remote_unidirectional_stream_limit_ += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FrameType::kMaxStreamsUnidirectional);
        frame->SetMaximumStreams(remote_unidirectional_stream_limit_);
        send_frame = frame;
    }
    return true;
}

}  // namespace quic
}  // namespace quicx
