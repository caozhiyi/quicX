#include <cstdint>
#include <cstddef>
#include <functional>
#include <unordered_map>

#include "quic/frame/if_frame.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/crypto_frame.h"
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
#include "common/buffer/single_block_buffer.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/retire_connection_id_frame.h"
#include "common/buffer/standalone_buffer_chunk.h"

static const std::unordered_map<uint16_t, std::function<std::shared_ptr<quicx::quic::IFrame>(uint16_t)>> kFrameCreatorMap = {
    {quicx::quic::FrameType::kPadding,                     [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::PaddingFrame>(); }},
    {quicx::quic::FrameType::kPing,                        [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::PingFrame>(); }},
    {quicx::quic::FrameType::kAck,                         [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::AckFrame>(); }},
    {quicx::quic::FrameType::kAckEcn,                      [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::AckEcnFrame>(); }},
    {quicx::quic::FrameType::kResetStream,                 [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::ResetStreamFrame>(); }},
    {quicx::quic::FrameType::kStopSending,                 [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StopSendingFrame>(); }},
    {quicx::quic::FrameType::kCrypto,                      [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::CryptoFrame>(); }},
    {quicx::quic::FrameType::kNewToken,                    [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::NewTokenFrame>(); }},
    {quicx::quic::FrameType::kStream,                      [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StreamFrame>(type); }},
    {quicx::quic::FrameType::kStream,                      [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StreamFrame>(type); }},
    {quicx::quic::FrameType::kMaxData,                     [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::MaxDataFrame>(); }},
    {quicx::quic::FrameType::kMaxStreamData,               [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::MaxStreamDataFrame>(); }},
    {quicx::quic::FrameType::kMaxStreamsBidirectional,     [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::MaxStreamsFrame>(type); }},
    {quicx::quic::FrameType::kMaxStreamsUnidirectional,    [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::MaxStreamsFrame>(type); }},
    {quicx::quic::FrameType::kDataBlocked,                 [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::DataBlockedFrame>(); }},
    {quicx::quic::FrameType::kStreamDataBlocked,           [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StreamDataBlockedFrame>(); }},
    {quicx::quic::FrameType::kStreamsBlockedBidirectional, [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StreamsBlockedFrame>(type); }},
    {quicx::quic::FrameType::kStreamsBlockedUnidirectional,[](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::StreamsBlockedFrame>(type); }},
    {quicx::quic::FrameType::kNewConnectionId,             [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::NewConnectionIDFrame>(); }},
    {quicx::quic::FrameType::kRetireConnectionId,          [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::RetireConnectionIDFrame>(); }},
    {quicx::quic::FrameType::kPathChallenge,               [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::PathChallengeFrame>(); }},
    {quicx::quic::FrameType::kPathResponse,                [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::PathResponseFrame>(); }},
    {quicx::quic::FrameType::kConnectionClose,             [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::ConnectionCloseFrame>(type); }},
    {quicx::quic::FrameType::kConnectionCloseApp,          [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::ConnectionCloseFrame>(type); }},
    {quicx::quic::FrameType::kHandshakeDone,               [](uint16_t type) -> std::shared_ptr<quicx::quic::IFrame> { return std::make_shared<quicx::quic::HandshakeDoneFrame>(); }},
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // 1) Fuzz AckFrame decode -> encode -> decode
    {
        // Wrap input as a read buffer
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(size));
        in->Write(data, size);

        for (auto iter = kFrameCreatorMap.begin(); iter != kFrameCreatorMap.end(); ++iter) {
            auto frame = iter->second(iter->first);
            if (frame) {
                frame->Decode(in, true);
            }

            uint8_t out_buf[2048];
            auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
                std::make_shared<quicx::common::StandaloneBufferChunk>(sizeof(out_buf)));
            out->Write(out_buf, sizeof(out_buf));
            (void)frame->Encode(out);

            (void)frame->Decode(out, true);
        }
    }

    return 0;
}


