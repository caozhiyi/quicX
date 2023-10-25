#include <cstring>
#include "common/log/log.h"
#include "quic/frame/type.h"
#include "common/util/time.h"
#include "common/buffer/buffer.h"
#include "quic/connection/util.h"
#include "quic/connection/error.h"
#include "quic/frame/stream_frame.h"
#include "quic/packet/init_packet.h"
#include "quic/stream/recv_stream.h"
#include "quic/stream/send_stream.h"
#include "quic/packet/retry_packet.h"
#include "quic/packet/rtt_0_packet.h"
#include "quic/packet/rtt_1_packet.h"
#include "quic/stream/crypto_stream.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/packet/handshake_packet.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/connection/base_connection.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start):
    _to_close(false),
    _last_communicate_time(0),
    _flow_control(start) {
    _alloter = MakeBlockMemoryPoolPtr(1024, 4);
    _connection_crypto.SetRemoteTransportParamCB(std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

}

BaseConnection::~BaseConnection() {

}

std::shared_ptr<ISendStream> BaseConnection::MakeSendStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    if(!_flow_control.CheckLocalUnidirectionStreamLimit(stream_id, frame)) {
        return nullptr;
    }
    if (frame) {
        _frames_list.push_back(frame);
    }
    
    auto stream = MakeStream(_transport_param.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_SEND);
    return std::dynamic_pointer_cast<ISendStream>(stream);
}

std::shared_ptr<BidirectionStream> BaseConnection::MakeBidirectionalStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    if(!_flow_control.CheckLocalBidirectionStreamLimit(stream_id, frame)) {
        return nullptr;
    }
    if (frame) {
        _frames_list.push_back(frame);
    }
    
    auto stream = MakeStream(_transport_param.GetInitialMaxStreamDataBidiLocal(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
    return std::dynamic_pointer_cast<BidirectionStream>(stream);
}

void BaseConnection::SetAddConnectionIDCB(ConnectionIDCB cb) {
    _add_connection_id_cb = cb;
}

void BaseConnection::SetRetireConnectionIDCB(ConnectionIDCB cb) {
    _retire_connection_id_cb = cb;
}

void BaseConnection::AddConnectionId(uint8_t* id, uint16_t len) {
    ConnectionID cid(id, len);
    _conn_id_manager.AddID(cid);
    if (_add_connection_id_cb) {
        _add_connection_id_cb(cid.Hash());
    }
}

void BaseConnection::RetireConnectionId(uint8_t* id, uint16_t len) {
    ConnectionID cid(id, len);
    _conn_id_manager.RetireID(cid);
    if (_retire_connection_id_cb) {
        _retire_connection_id_cb(cid.Hash());
    }
}

void BaseConnection::Close(uint64_t error) {
    _to_close = true;
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    _frames_list.push_back(frame);
}

bool BaseConnection::GenerateSendData(std::shared_ptr<IBuffer> buffer) {
    // check flow control
    uint32_t can_send_size;
    std::shared_ptr<IFrame> frame;
    if (!_flow_control.CheckLocalSendDataLimit(can_send_size, frame)) {
        return false;
    }
    if (frame) {
        _frames_list.push_back(frame);
    }
    
    while (true) {
        if (_frames_list.empty() && _active_send_stream_list.empty()) {
            break;
        }
        
        // TODO put 1450 to config
        FixBufferFrameVisitor frame_visitor(1450);
        frame_visitor.SetStreamDataSizeLimit(can_send_size);
        // priority sending frames of connection
        for (auto iter = _frames_list.begin(); iter != _frames_list.end();) {
            if (frame_visitor.HandleFrame(*iter)) {
                iter = _frames_list.erase(iter);

            } else {
                return false;
            }
        }

        // then sending frames of stream
        for (auto iter = _active_send_stream_list.begin(); iter != _active_send_stream_list.end();) {
            auto ret = (*iter)->TrySendData(&frame_visitor);
            if (ret == IStream::TSR_SUCCESS) {
                iter = _active_send_stream_list.erase(iter);
    
            } else if (ret == IStream::TSR_FAILED) {
                return false;
    
            } else if (ret == IStream::TSR_BREAK) {
                iter = _active_send_stream_list.erase(iter);
                break;
            }
        }

        // make quic packet
        std::shared_ptr<IPacket> packet;
        uint8_t encrypto_level = GetCurEncryptionLevel();
        uint8_t packet_encrypto_level = frame_visitor.GetEncryptionLevel();
        if (packet_encrypto_level < encrypto_level) {
            encrypto_level = packet_encrypto_level;
        }

        switch (encrypto_level) {
            case EL_INITIAL: {
                auto init_packet = std::make_shared<InitPacket>();
                init_packet->SetPayload(frame_visitor.GetBuffer()->GetReadSpan());
                uint8_t dcid[10] = {0,1,2,3,4,5,6,7,8,9};
                ((LongHeader*)init_packet->GetHeader())->SetDestinationConnectionId(dcid, sizeof(dcid));
                packet = init_packet;
                break;
            }
            case EL_EARLY_DATA: {
                packet = std::make_shared<Rtt0Packet>();
                break;
            }
            case EL_HANDSHAKE: {
                auto handshake_packet = std::make_shared<HandshakePacket>();
                handshake_packet->SetPayload(frame_visitor.GetBuffer()->GetReadSpan());
                packet = handshake_packet;
                break;
            }
            case EL_APPLICATION: {
                auto rtt1_packet = std::make_shared<Rtt1Packet>();
                rtt1_packet->SetPayload(frame_visitor.GetBuffer()->GetReadSpan());
                packet = rtt1_packet;
                break;
            }
        }

        // make packet numer
        uint64_t pkt_number = _pakcet_number.NextPakcetNumber(CryptoLevel2PacketNumberSpace(encrypto_level));
        packet->SetPacketNumber(pkt_number);
        packet->GetHeader()->SetPacketNumberLength(PacketNumber::GetPacketNumberLength(pkt_number));

        auto crypto_grapher = _connection_crypto.GetCryptographer(encrypto_level);
        if (!crypto_grapher) {
            LOG_ERROR("encrypt grapher is not ready.");
            return false;
        }

        if (!packet->Encode(buffer, crypto_grapher)) {
            LOG_ERROR("packet encode failed.");
            return false;
        }

        _flow_control.AddLocalSendData(frame_visitor.GetStreamDataSize());
        can_send_size -= frame_visitor.GetStreamDataSize();
    }

    return true;
}

void BaseConnection::OnPackets(std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        switch (packets[i]->GetHeader()->GetPacketType())
        {
        case PT_INITIAL:
            if (!OnInitialPacket(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                LOG_ERROR("init packet handle failed.");
            }
            break;
        case PT_0RTT:
            if (!On0rttPacket(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PT_HANDSHAKE:
            if (!OnHandshakePacket(std::dynamic_pointer_cast<HandshakePacket>(packets[i]))) {
                LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PT_RETRY:
            if (!OnRetryPacket(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PT_1RTT:
            if (!On1rttPacket(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                LOG_ERROR("1 rtt packet handle failed.");
            }
            break;
        default:
            LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
            break;
        }
    }
}


bool BaseConnection::OnInitialPacket(std::shared_ptr<IPacket> packet) {
    // check init packet size

    if (!_connection_crypto.InitIsReady()) {
        LongHeader* header = (LongHeader*)packet->GetHeader();
        _connection_crypto.InstallInitSecret((uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);
    }
    return OnNormalPacket(packet);
}


bool BaseConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnRetryPacket(std::shared_ptr<IPacket> packet) {
    return true;
}

bool BaseConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames) {
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        switch (type)
        {
        case FT_PADDING:
            // do nothing
            break;
        case FT_PING: 
            _last_communicate_time = UTCTimeMsec();
            break;
        case FT_ACK: break;
        case FT_ACK_ECN: break;
        case FT_CRYPTO:
            return OnCryptoFrame(frames[i]);
        case FT_NEW_TOKEN:
            return OnNewTokenFrame(frames[i]);
        case FT_MAX_DATA:
            return OnMaxDataFrame(frames[i]);
        case FT_MAX_STREAMS_BIDIRECTIONAL:
        case FT_MAX_STREAMS_UNIDIRECTIONAL:
            return OnMaxStreamFrame(frames[i]);
        case FT_DATA_BLOCKED: 
            return OnDataBlockFrame(frames[i]);
        case FT_STREAMS_BLOCKED_BIDIRECTIONAL:
        case FT_STREAMS_BLOCKED_UNIDIRECTIONAL:
            return OnStreamBlockFrame(frames[i]);
        case FT_NEW_CONNECTION_ID: break;
        case FT_RETIRE_CONNECTION_ID: break;
        case FT_PATH_CHALLENGE: break;
        case FT_PATH_RESPONSE: break;
        case FT_CONNECTION_CLOSE: break;
        case FT_CONNECTION_CLOSE_APP: break;
        case FT_HANDSHAKE_DONE: break;
        case FT_RESET_STREAM:
        case FT_STOP_SENDING:
        case FT_STREAM_DATA_BLOCKED:
        case FT_MAX_STREAM_DATA:
            return OnStreamFrame(frames[i]);
        default:
            if (StreamFrame::IsStreamFrame(type)) {
                return OnStreamFrame(frames[i]);
            }
        }
    }
    return false;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream = _streams_map.find(stream_id);
    if (stream != _streams_map.end()) {
        _flow_control.AddRemoteSendData(stream->second->OnFrame(frame));
        return true;
    }

    // check streams limit    
    std::shared_ptr<IFrame> send_frame;
    if (!_flow_control.CheckRemoteStreamLimit(stream_id, send_frame)) {
        return false;
    }
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }

    // frame 的序列化函数
    
    
    // create new stream
    std::shared_ptr<IStream> new_stream;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::SD_BIDIRECTIONAL) {
        new_stream = MakeStream(_transport_param.GetInitialMaxStreamDataBidiRemote(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
        
    } else {
        new_stream = MakeStream(_transport_param.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_RECV);
    }
    _streams_map[stream_id] = new_stream;
    _flow_control.AddRemoteSendData(new_stream->OnFrame(frame));

    // check peer data limit
    if (!_flow_control.CheckRemoteSendDataLimit(send_frame)) {
        InnerConnectionClose(QEC_FLOW_CONTROL_ERROR, frame->GetType(), "flow control stream data limit.");
        return false;
    }
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }

    return true;
}

bool BaseConnection::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    _connection_crypto.OnCryptoFrame(frame);
    return true;
}

bool BaseConnection::OnNewTokenFrame(std::shared_ptr<IFrame> frame) {
    auto token_frame = std::dynamic_pointer_cast<NewTokenFrame>(frame);
    auto data = token_frame->GetToken();
    _token = std::move(std::string((const char*)data, token_frame->GetTokenLength()));
    return true;
}

bool BaseConnection::OnMaxDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxDataFrame>(frame);
    uint64_t max_data_size = max_data_frame->GetMaximumData();
    _flow_control.UpdateLocalSendDataLimit(max_data_size);
    return true;
}

bool BaseConnection::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    _flow_control.CheckRemoteSendDataLimit(send_frame);
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    _flow_control.CheckRemoteStreamLimit(0, send_frame);
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);
    if (stream_block_frame->GetType() == FT_MAX_STREAMS_BIDIRECTIONAL) {
        _flow_control.UpdateLocalBidirectionStreamLimit(stream_block_frame->GetMaximumStreams());

    } else {
        _flow_control.UpdateLocalUnidirectionStreamLimit(stream_block_frame->GetMaximumStreams());
    }
    return true;
}

void BaseConnection::ActiveSendStream(IStream* stream) {
    _active_send_stream_list.emplace_back(stream);
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion) {
    _to_close = true;
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    frame->SetErrFrameType(tigger_frame);
    frame->SetReason(resion);
    _frames_list.push_back(frame);
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // remove stream
    auto stream = _streams_map.find(stream_id);
    if (stream != _streams_map.end()) {
        _streams_map.erase(stream);
    }
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    _transport_param.Merge(remote_tp);
    _flow_control.InitConfig(_transport_param);
}

bool BaseConnection::OnNormalPacket(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<ICryptographer> cryptographer = _connection_crypto.GetCryptographer(packet->GetCryptoLevel());
    if (!cryptographer) {
        LOG_ERROR("decrypt grapher is not ready.");
        return false;
    }
    
    uint8_t buf[1450];
    std::shared_ptr<IBuffer> out_plaintext = std::make_shared<Buffer>(buf, 1450);
    if (!packet->Decode(out_plaintext, cryptographer)) {
        LOG_ERROR("decode packet after decrypt failed.");
        return false;
    }

    if (!OnFrames(packet->GetFrames())) {
        LOG_ERROR("process frames failed.");
        return false;
    }
    return true;
}

std::shared_ptr<IStream> BaseConnection::MakeStream(uint32_t init_size, uint64_t stream_id, StreamType st) {
    std::shared_ptr<IStream> new_stream;
    if (st == BaseConnection::ST_BIDIRECTIONAL) {
        new_stream = std::make_shared<BidirectionStream>(_alloter, init_size, stream_id);

    } else if (st == BaseConnection::ST_SEND) {
        new_stream = std::make_shared<SendStream>(_alloter, init_size, stream_id);

    } else {
        new_stream = std::make_shared<RecvStream>(_alloter, init_size, stream_id);
    }
    new_stream->SetStreamCloseCB(std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1));
    new_stream->SetConnectionCloseCB(std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    new_stream->SetHopeSendCB(std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1));
    return new_stream;
}

}