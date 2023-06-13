#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/connection/connection_flow_control.h"

namespace quicx {

ConnectionFlowControl::ConnectionFlowControl(StreamIDGenerator::StreamStarter starter):
    _local_send_data_size(0),
    _remote_send_data_size(0),
    _local_max_bidirectional_stream_id(0),
    _local_max_unidirectional_stream_id(0),
    _remote_max_bidirectional_stream_id(0),
    _remote_max_unidirectional_stream_id(0),
    _id_generator(starter) {

}

void ConnectionFlowControl::InitConfig(TransportParam& tp) {
    _local_send_max_data_limit = tp.GetInitialMaxData();
    _remote_send_max_data_limit = tp.GetInitialMaxData();
    _local_bidirectional_stream_limit = tp.GetInitialMaxStreamsBidi();
    _local_unidirectional_stream_limit = tp.GetInitialMaxStreamsUni();
    _remote_bidirectional_stream_limit = tp.GetInitialMaxStreamsBidi();
    _remote_unidirectional_stream_limit = tp.GetInitialMaxStreamsUni();
}

void ConnectionFlowControl::AddLocalSendData(uint32_t size) {
    _local_send_data_size += size;
}

void ConnectionFlowControl::UpdateLocalSendDataLimit(uint64_t limit) {
    if (_local_send_max_data_limit < limit) {
        _local_send_max_data_limit = limit;
    }
}

bool ConnectionFlowControl::CheckLocalSendDataLimit(uint32_t& can_send_size, std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (_local_send_data_size >= _local_send_max_data_limit) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(_local_send_max_data_limit);
        send_frame = frame;
        return false;
    }
    
    // TODO put 8912 to config
    can_send_size = _local_send_max_data_limit - _local_send_data_size;
    if (_local_send_max_data_limit - _local_send_data_size < 8912) {
        auto frame = std::make_shared<DataBlockedFrame>();
        frame->SetMaximumData(_local_send_max_data_limit);
        send_frame = frame;
    }
    return true;
}

void ConnectionFlowControl::AddRemoteSendData(uint32_t size) {
    _remote_send_data_size += size;
}

bool ConnectionFlowControl::CheckRemoteSendDataLimit(std::shared_ptr<IFrame>& send_frame) {
    // reaching the upper limit of flow control
    if (_remote_send_data_size >= _remote_send_max_data_limit) {
        return false;
    }

    // check remote data limit. TODO put 8912 to config
    if (_remote_send_max_data_limit - _remote_send_data_size < 8912) {
        _remote_send_max_data_limit += 8912;
        auto frame = std::make_shared<MaxDataFrame>();
        frame->SetMaximumData(_remote_send_max_data_limit);
        send_frame = frame;
    }
    return true;
}

void ConnectionFlowControl::UpdateLocalBidirectionStreamLimit(uint64_t limit) {
    if (_local_bidirectional_stream_limit < limit) {
        _local_bidirectional_stream_limit = limit;
    }
}

bool ConnectionFlowControl::CheckLocalBidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    stream_id = _id_generator.NextStreamID(StreamIDGenerator::SD_BIDIRECTIONAL);
    // reaching the upper limit of flow control
    if (stream_id >> 2 > _local_bidirectional_stream_limit) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FT_STREAMS_BLOCKED_BIDIRECTIONAL);
        frame->SetMaximumStreams(_local_bidirectional_stream_limit);
        send_frame = frame;
        return false;
    }

    // TODO put 4 to config
    if (_local_bidirectional_stream_limit - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FT_STREAMS_BLOCKED_BIDIRECTIONAL);
        frame->SetMaximumStreams(_local_bidirectional_stream_limit);
        send_frame = frame;
    }
    _local_max_bidirectional_stream_id = stream_id;
    return true;
}

void ConnectionFlowControl::UpdateLocalUnidirectionStreamLimit(uint64_t limit) {
    if (_local_unidirectional_stream_limit < limit) {
        _local_unidirectional_stream_limit = limit;
    }
}

bool ConnectionFlowControl::CheckLocalUnidirectionStreamLimit(uint64_t& stream_id, std::shared_ptr<IFrame>& send_frame) {
    stream_id = _id_generator.NextStreamID(StreamIDGenerator::SD_UNIDIRECTIONAL);
    // reaching the upper limit of flow control
    if (stream_id >> 2 > _local_unidirectional_stream_limit) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FT_STREAMS_BLOCKED_UNIDIRECTIONAL);
        frame->SetMaximumStreams(_local_unidirectional_stream_limit);
        send_frame = frame;
        return false;
    }

    // TODO put 4 to config
    if (_local_unidirectional_stream_limit - (stream_id >> 2) < 4) {
        auto frame = std::make_shared<StreamsBlockedFrame>(FT_STREAMS_BLOCKED_UNIDIRECTIONAL);
        frame->SetMaximumStreams(_local_unidirectional_stream_limit);
        send_frame = frame;
    }
    _local_max_unidirectional_stream_id = stream_id;
    return true;
}

bool ConnectionFlowControl::CheckRemoteStreamLimit(uint64_t id, std::shared_ptr<IFrame>& send_frame) {
    if (StreamIDGenerator::GetStreamDirection(id) == StreamIDGenerator::StreamDirection::SD_UNIDIRECTIONAL)  {
        if (id > 0) {
            _remote_max_unidirectional_stream_id = id;
        }
        return CheckRemoteUnidirectionStreamLimit(send_frame);
    } else {
        if (id > 0) {
            _remote_max_bidirectional_stream_id = id;
        }
        return CheckRemoteBidirectionStreamLimit(send_frame);
    }
}

bool ConnectionFlowControl::CheckRemoteBidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    if (_remote_bidirectional_stream_limit < _remote_max_bidirectional_stream_id >> 2) {
        return false;
    }

    if (_remote_bidirectional_stream_limit - (_remote_max_bidirectional_stream_id >> 2) < 4) {
        _remote_bidirectional_stream_limit += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FT_MAX_STREAMS_BIDIRECTIONAL);
        frame->SetMaximumStreams(_remote_bidirectional_stream_limit);
        send_frame = frame;
    }
    return true;
}

bool ConnectionFlowControl::CheckRemoteUnidirectionStreamLimit(std::shared_ptr<IFrame>& send_frame) {
    if (_remote_unidirectional_stream_limit < _remote_max_unidirectional_stream_id >> 2) {
        return false;
    }

    if (_remote_unidirectional_stream_limit - (_remote_max_unidirectional_stream_id >> 2) < 4) {
        _remote_unidirectional_stream_limit += 8;
        auto frame = std::make_shared<MaxStreamsFrame>(FT_MAX_STREAMS_UNIDIRECTIONAL);
        frame->SetMaximumStreams(_remote_unidirectional_stream_limit);
        send_frame = frame;
    }
    return true;
}

}
