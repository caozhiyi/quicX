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
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start,
        std::shared_ptr<common::ITimer> timer,
        ConnectionIDCB add_conn_id_cb,
        ConnectionIDCB retire_conn_id_cb):
    _to_close(false),
    _last_communicate_time(0),
    _recv_control(timer),
    _send_manager(timer),
    _is_active_send(false) {
    _alloter = common::MakeBlockMemoryPoolPtr(1024, 4);
    _connection_crypto.SetRemoteTransportParamCB(std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));
    _flow_control = std::make_shared<FlowControl>(start);
    _remote_conn_id_manager = std::make_shared<ConnectionIDManager>();
    _local_conn_id_manager = std::make_shared<ConnectionIDManager>();
    _local_conn_id_manager->SetAddConnectionIDCB(add_conn_id_cb);
    _local_conn_id_manager->SetRetireConnectionIDCB(retire_conn_id_cb);

    _send_manager.SetFlowControl(_flow_control);
    _send_manager.SetRemoteConnectionIDManager(_remote_conn_id_manager);
    _send_manager.SetLocalConnectionIDManager(_local_conn_id_manager);
    
    // generate local id
    _local_conn_id_manager->Generator();
}

BaseConnection::~BaseConnection() {

}

std::shared_ptr<ISendStream> BaseConnection::MakeSendStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = _flow_control->CheckLocalUnidirectionStreamLimit(stream_id, frame);
    if (frame) {
        _frames_list.push_back(frame);
    }
    if (!can_make_stream) {
        return nullptr;
    }

    auto stream = MakeStream(_transport_param.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_SEND);
    return std::dynamic_pointer_cast<ISendStream>(stream);
}

std::shared_ptr<BidirectionStream> BaseConnection::MakeBidirectionalStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = _flow_control->CheckLocalBidirectionStreamLimit(stream_id, frame);
    if (frame) {
        _frames_list.push_back(frame);
    }
    if (!can_make_stream) {
        return nullptr;
    }
    
    auto stream = MakeStream(_transport_param.GetInitialMaxStreamDataBidiLocal(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
    return std::dynamic_pointer_cast<BidirectionStream>(stream);
}

void BaseConnection::Close(uint64_t error) {
    _to_close = true;
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    _frames_list.push_back(frame);
}

bool BaseConnection::GenerateSendData(std::shared_ptr<common::IBuffer> buffer) {
    // make quic packet
    uint8_t encrypto_level = GetCurEncryptionLevel();
    auto crypto_grapher = _connection_crypto.GetCryptographer(encrypto_level);
    if (!crypto_grapher) {
        common::LOG_ERROR("encrypt grapher is not ready.");
        return false;
    }

    auto ack_frame = _recv_control.MayGenerateAckFrame(common::UTCTimeMsec(), CryptoLevel2PacketNumberSpace(encrypto_level));
    if (ack_frame) {
        _send_manager.AddFrame(ack_frame);
    }
    
    return _send_manager.GetSendData(buffer, encrypto_level, crypto_grapher);
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        _recv_control.OnPacketRecv(now, packets[i]);

        switch (packets[i]->GetHeader()->GetPacketType())
        {
        case PT_INITIAL:
            if (!OnInitialPacket(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                common::LOG_ERROR("init packet handle failed.");
            }
            break;
        case PT_0RTT:
            if (!On0rttPacket(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                common::LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PT_HANDSHAKE:
            if (!OnHandshakePacket(std::dynamic_pointer_cast<HandshakePacket>(packets[i]))) {
                common::LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PT_RETRY:
            if (!OnRetryPacket(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                common::LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PT_1RTT:
            if (!On1rttPacket(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                common::LOG_ERROR("1 rtt packet handle failed.");
            }
            break;
        default:
            common::LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
            break;
        }
    }
}

void BaseConnection::SetActiveConnectionCB(ActiveConnectionCB cb) {
    _active_connection_cb = cb;
    _recv_control.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));
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

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level) {
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        switch (type)
        {
        case FT_PADDING:
            // do nothing
            break;
        case FT_PING: 
            _last_communicate_time = common::UTCTimeMsec();
            break;
        case FT_ACK:
        case FT_ACK_ECN:
            return OnAckFrame(frames[i], crypto_level);
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
        case FT_NEW_CONNECTION_ID: 
            return OnNewConnectionIDFrame(frames[i]);
        case FT_RETIRE_CONNECTION_ID:
            return OnRetireConnectionIDFrame(frames[i]);
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
            } else {
                common::LOG_ERROR("invalid frame type. type:%s", type);
            }
        }
    }
    return false;
}

bool BaseConnection::OnAckFrame(std::shared_ptr<IFrame> frame,  uint16_t crypto_level) {
    auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
    _send_manager.OnPacketAck(ns, frame);
    return true;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream = _streams_map.find(stream_id);
    if (stream != _streams_map.end()) {
        _flow_control->AddRemoteSendData(stream->second->OnFrame(frame));
        return true;
    }

    // check streams limit    
    std::shared_ptr<IFrame> send_frame;
    bool can_make_stream = _flow_control->CheckRemoteStreamLimit(stream_id, send_frame);
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }
    if (!can_make_stream) {
        return false;
    }
    
    // create new stream
    std::shared_ptr<IStream> new_stream;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::SD_BIDIRECTIONAL) {
        new_stream = MakeStream(_transport_param.GetInitialMaxStreamDataBidiRemote(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
        
    } else {
        new_stream = MakeStream(_transport_param.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_RECV);
    }
    _streams_map[stream_id] = new_stream;
    _flow_control->AddRemoteSendData(new_stream->OnFrame(frame));

    // check peer data limit
    if (!_flow_control->CheckRemoteSendDataLimit(send_frame)) {
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
    if (!token_frame) {
        common::LOG_ERROR("invalid new token frame.");
        return false;
    }
    auto data = token_frame->GetToken();
    _token = std::move(std::string((const char*)data, token_frame->GetTokenLength()));
    return true;
}

bool BaseConnection::OnMaxDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxDataFrame>(frame);
    if (!max_data_frame) {
        common::LOG_ERROR("invalid max data frame.");
        return false;
    }
    uint64_t max_data_size = max_data_frame->GetMaximumData();
    _flow_control->UpdateLocalSendDataLimit(max_data_size);
    return true;
}

bool BaseConnection::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    _flow_control->CheckRemoteSendDataLimit(send_frame);
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    _flow_control->CheckRemoteStreamLimit(0, send_frame);
    if (send_frame) {
        _frames_list.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);
    if (stream_block_frame->GetType() == FT_MAX_STREAMS_BIDIRECTIONAL) {
        _flow_control->UpdateLocalBidirectionStreamLimit(stream_block_frame->GetMaximumStreams());

    } else {
        _flow_control->UpdateLocalUnidirectionStreamLimit(stream_block_frame->GetMaximumStreams());
    }
    return true;
}

bool BaseConnection::OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto new_cid_frame = std::dynamic_pointer_cast<NewConnectionIDFrame>(frame);
    if (!new_cid_frame) {
        common::LOG_ERROR("invalid new connection id frame.");
        return false;
    }
    
    _remote_conn_id_manager->RetireIDBySequence(new_cid_frame->GetRetirePriorTo());
    ConnectionID id;
    new_cid_frame->GetConnectionID(id._id, id._len);
    id._index = new_cid_frame->GetSequenceNumber();
    _remote_conn_id_manager->AddID(id);
    return true;
}

bool BaseConnection::OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto retire_cid_frame = std::dynamic_pointer_cast<RetireConnectionIDFrame>(frame);
    _remote_conn_id_manager->RetireIDBySequence(retire_cid_frame->GetSequenceNumber());
    return true;
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    _send_manager.AddActiveStream(stream);
    ActiveSend();
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion) {
    _to_close = true;
    // make connection close frame
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
    _flow_control->InitConfig(_transport_param);
}

bool BaseConnection::OnNormalPacket(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<ICryptographer> cryptographer = _connection_crypto.GetCryptographer(packet->GetCryptoLevel());
    if (!cryptographer) {
        common::LOG_ERROR("decrypt grapher is not ready.");
        return false;
    }
    
    packet->SetCryptographer(cryptographer);
    
    uint8_t buf[1450] = {0};
    std::shared_ptr<common::IBuffer> out_plaintext = std::make_shared<common::Buffer>(buf, 1450);
    if (!packet->DecodeWithCrypto(out_plaintext)) {
        common::LOG_ERROR("decode packet after decrypt failed.");
        return false;
    }

    if (!OnFrames(packet->GetFrames(), packet->GetCryptoLevel())) {
        common::LOG_ERROR("process frames failed.");
        return false;
    }
    return true;
}

void BaseConnection::ActiveSend() {
    if (!_is_active_send) {
        _is_active_send = true;
    }
    if (_active_connection_cb) {
        _active_connection_cb(shared_from_this());
    }
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
    new_stream->SetActiveStreamSendCB(std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1));
    return new_stream;
}

}
}