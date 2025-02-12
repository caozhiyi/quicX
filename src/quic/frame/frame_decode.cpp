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

// frame type to craeter function map
static const std::unordered_map<uint16_t, std::function<std::shared_ptr<IFrame>(uint16_t)>> kFrameCreatorMap = {
    {FrameType::kPadding,                     [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PaddingFrame>(); }},
    {FrameType::kPing,                        [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PingFrame>(); }},
    {FrameType::kAck,                         [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<AckFrame>(); }},
    {FrameType::kAckEcn,                      [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<AckEcnFrame>(); }},
    {FrameType::kResetStream,                 [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ResetStreamFrame>(); }},
    {FrameType::kStopSending,                 [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StopSendingFrame>(); }},
    {FrameType::kCrypto,                      [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<CryptoFrame>(); }},
    {FrameType::kNewToken,                    [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<NewTokenFrame>(); }},
    {FrameType::kStream,                      [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 1,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 2,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 3,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 4,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 5,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 6,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kStream + 7,                  [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamFrame>(type); }},
    {FrameType::kMaxData,                     [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxDataFrame>(); }},
    {FrameType::kMaxStreamData,               [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamDataFrame>(); }},
    {FrameType::kMaxStreamsBidirectional,     [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamsFrame>(type); }},
    {FrameType::kMaxStreamsUnidirectional,    [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<MaxStreamsFrame>(type); }},
    {FrameType::kDataBlocked,                 [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<DataBlockedFrame>(); }},
    {FrameType::kStreamDataBlocked,           [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamDataBlockedFrame>(); }},
    {FrameType::kStreamsBlockedBidirectional, [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamsBlockedFrame>(type); }},
    {FrameType::kStreamsBlockedUnidirectional,[](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<StreamsBlockedFrame>(type); }},
    {FrameType::kNewConnectionId,             [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<NewConnectionIDFrame>(); }},
    {FrameType::kRetireConnectionId,          [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<RetireConnectionIDFrame>(); }},
    {FrameType::kPathChallenge,               [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PathChallengeFrame>(); }},
    {FrameType::kPathResponse,                [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<PathResponseFrame>(); }},
    {FrameType::kConnectionClose,             [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ConnectionCloseFrame>(type); }},
    {FrameType::kConnectionCloseApp,          [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<ConnectionCloseFrame>(type); }},
    {FrameType::kHandshakeDone,               [](uint16_t type) -> std::shared_ptr<IFrame> { return std::make_shared<HandshakeDoneFrame>(); }},
};

bool DecodeFrames(std::shared_ptr<common::IBufferRead> buffer, std::vector<std::shared_ptr<IFrame>>& frames) {
    if(buffer->GetDataLength() == 0) {
        return false;
    }

    static const uint8_t kTypeBufLength = 2;

    std::shared_ptr<IFrame> frame;
    uint16_t frame_type = 0;
    uint8_t type_buf[kTypeBufLength] = {0};

    while (buffer->GetDataLength() > 0) {
        // decode type
        if (buffer->Read(type_buf, kTypeBufLength) != kTypeBufLength) {
            common::LOG_ERROR("wrong buffer size while read frame type. size:%d", buffer->GetDataLength());
            return false;
        }
        common::FixedDecodeUint16(type_buf, type_buf + kTypeBufLength, frame_type);

        auto creator = kFrameCreatorMap.find(frame_type);
        if (creator != kFrameCreatorMap.end()) {
            // create frame
            frame = creator->second(frame_type);

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

}
}
