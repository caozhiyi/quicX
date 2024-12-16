#include <functional>
#include <unordered_map>


#include "common/log/log.h"
#include "common/decode/decode.h"
#include "common/util/singleton.h"
#include "common/buffer/if_buffer.h"

#include "quic/frame/ack_frame.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/frame/padding_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

class FrameDecode:
    public common::Singleton<FrameDecode> {
public:
    FrameDecode();
    ~FrameDecode();

    bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames);
private:
    typedef std::function<std::shared_ptr<IFrame>(uint16_t)> FrameCreater;
    // frame type to craeter function map
    static std::unordered_map<uint16_t, FrameCreater> __frame_creater_map;
};

std::unordered_map<uint16_t, FrameDecode::FrameCreater> FrameDecode::__frame_creater_map;

FrameDecode::FrameDecode() {
    __frame_creater_map[FT_PADDING]                         = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PaddingFrame>(); };
    __frame_creater_map[FT_PING]                            = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PingFrame>(); };
    __frame_creater_map[FT_ACK]                             = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<AckFrame>(); };
    __frame_creater_map[FT_ACK_ECN]                         = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<AckEcnFrame>(); };
    __frame_creater_map[FT_RESET_STREAM]                    = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ResetStreamFrame>(); };
    __frame_creater_map[FT_STOP_SENDING]                    = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StopSendingFrame>(); };
    __frame_creater_map[FT_CRYPTO]                          = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<CryptoFrame>(); };
    __frame_creater_map[FT_NEW_TOKEN]                       = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<NewTokenFrame>(); };
    __frame_creater_map[FT_STREAM]                          = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 1]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 2]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 3]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 4]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 5]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 6]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_STREAM + 7]                      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); };
    __frame_creater_map[FT_MAX_DATA]                        = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxDataFrame>(); };
    __frame_creater_map[FT_MAX_STREAM_DATA]                 = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamDataFrame>(); };
    __frame_creater_map[FT_MAX_STREAMS_BIDIRECTIONAL]       = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamsFrame>(type); };
    __frame_creater_map[FT_MAX_STREAMS_UNIDIRECTIONAL]      = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamsFrame>(type); };
    __frame_creater_map[FT_DATA_BLOCKED]                    = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<DataBlockedFrame>(); };
    __frame_creater_map[FT_STREAM_DATA_BLOCKED]             = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamDataBlockedFrame>(); };
    __frame_creater_map[FT_STREAMS_BLOCKED_BIDIRECTIONAL]   = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamsBlockedFrame>(type); };
    __frame_creater_map[FT_STREAMS_BLOCKED_UNIDIRECTIONAL]  = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamsBlockedFrame>(type); };
    __frame_creater_map[FT_NEW_CONNECTION_ID]               = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<NewConnectionIDFrame>(); };
    __frame_creater_map[FT_RETIRE_CONNECTION_ID]            = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<RetireConnectionIDFrame>(); };
    __frame_creater_map[FT_PATH_CHALLENGE]                  = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PathChallengeFrame>(); };
    __frame_creater_map[FT_PATH_RESPONSE]                   = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PathResponseFrame>(); };
    __frame_creater_map[FT_CONNECTION_CLOSE]                = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ConnectionCloseFrame>(type); };
    __frame_creater_map[FT_CONNECTION_CLOSE_APP]            = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ConnectionCloseFrame>(type); };
    __frame_creater_map[FT_HANDSHAKE_DONE]                  = [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<HandshakeDoneFrame>(); };
}

FrameDecode::~FrameDecode() {

}

bool FrameDecode::DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    if(buffer->GetDataLength() == 0) {
        return false;
    }

    static const uint8_t __type_buf_length = 2;

    std::shared_ptr<IFrame> frame;
    uint16_t frame_type = 0;
    uint8_t type_buf[__type_buf_length] = {0};

    while (buffer->GetDataLength() > 0) {
        // decode type
        if (buffer->Read(type_buf, __type_buf_length) != __type_buf_length) {
            common::LOG_ERROR("wrong buffer size while read frame type. size:%d", buffer->GetDataLength());
            return false;
        }
        common::FixedDecodeUint16(type_buf, type_buf + __type_buf_length, frame_type);

        auto creater = __frame_creater_map.find(frame_type);
        if (creater != __frame_creater_map.end()) {
            // create frame
            frame = creater->second(frame_type);

        } else {
            common::LOG_ERROR("invalid frame type. type:%d", frame_type);
            return false;
        }

        // decode frame
        if (!frame->Decode(buffer)) {
            common::LOG_ERROR("decode frame failed. frame type:%d", frame_type);
            return false;
        }
        frames.push_back(frame);
    }
    return true;
}

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    return FrameDecode::Instance().DecodeFrames(buffer, frames);
}

}
}
