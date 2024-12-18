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
        std::function<void(uint64_t, std::shared_ptr<IConnection>)> add_conn_id_cb,
        std::function<void(uint64_t)> retire_conn_id_cb):
    to_close_(false),
    last_communicate_time_(0),
    recv_control_(timer),
    send_manager_(timer),
    is_active_send_(false),
    add_conn_id_cb_(add_conn_id_cb) {

    alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4);
    connection_crypto_.SetRemoteTransportParamCB(std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    remote_conn_id_manager_ = std::make_shared<ConnectionIDManager>();
    local_conn_id_manager_ = std::make_shared<ConnectionIDManager>(std::bind(&BaseConnection::AddConnectionId, this, std::placeholders::_1),
        retire_conn_id_cb);

    flow_control_ = std::make_shared<FlowControl>(start);
    send_manager_.SetFlowControl(flow_control_);
    send_manager_.SetRemoteConnectionIDManager(remote_conn_id_manager_);
    send_manager_.SetLocalConnectionIDManager(local_conn_id_manager_);
}

BaseConnection::~BaseConnection() {

}

uint64_t BaseConnection::GetConnectionIDHash() {
    return local_conn_id_manager_->GetCurrentID().Hash();
}

std::shared_ptr<ISendStream> BaseConnection::MakeSendStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = flow_control_->CheckLocalUnidirectionStreamLimit(stream_id, frame);
    if (frame) {
        frames_list_.push_back(frame);
    }
    if (!can_make_stream) {
        return nullptr;
    }

    auto stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_SEND);
    return std::dynamic_pointer_cast<ISendStream>(stream);
}

std::shared_ptr<BidirectionStream> BaseConnection::MakeBidirectionalStream() {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = flow_control_->CheckLocalBidirectionStreamLimit(stream_id, frame);
    if (frame) {
        frames_list_.push_back(frame);
    }
    if (!can_make_stream) {
        return nullptr;
    }
    
    auto stream = MakeStream(transport_param_.GetInitialMaxStreamDataBidiLocal(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
    return std::dynamic_pointer_cast<BidirectionStream>(stream);
}

void BaseConnection::Close(uint64_t error) {
    to_close_ = true;
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    frames_list_.push_back(frame);
}

bool BaseConnection::GenerateSendData(std::shared_ptr<common::IBuffer> buffer) {
    // make quic packet
    uint8_t encrypto_level = GetCurEncryptionLevel();
    auto crypto_grapher = connection_crypto_.GetCryptographer(encrypto_level);
    if (!crypto_grapher) {
        common::LOG_ERROR("encrypt grapher is not ready.");
        return false;
    }

    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), CryptoLevel2PacketNumberSpace(encrypto_level));
    if (ack_frame) {
        send_manager_.AddFrame(ack_frame);
    }
    
    return send_manager_.GetSendData(buffer, encrypto_level, crypto_grapher);
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        recv_control_.OnPacketRecv(now, packets[i]);

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

void BaseConnection::SetActiveConnectionCB(std::function<void(std::shared_ptr<IConnection>)> cb) {
    active_connection_cb_ = cb;
    recv_control_.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));
}

bool BaseConnection::OnInitialPacket(std::shared_ptr<IPacket> packet) {
    // TODO check init packet size

    if (!connection_crypto_.InitIsReady()) {
        LongHeader* header = (LongHeader*)packet->GetHeader();
        connection_crypto_.InstallInitSecret((uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);
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
            last_communicate_time_ = common::UTCTimeMsec();
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
    send_manager_.OnPacketAck(ns, frame);
    return true;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        flow_control_->AddRemoteSendData(stream->second->OnFrame(frame));
        return true;
    }

    // check streams limit    
    std::shared_ptr<IFrame> send_frame;
    bool can_make_stream = flow_control_->CheckRemoteStreamLimit(stream_id, send_frame);
    if (send_frame) {
        frames_list_.push_back(send_frame);
    }
    if (!can_make_stream) {
        return false;
    }
    
    // create new stream
    std::shared_ptr<IStream> new_stream;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::SD_BIDIRECTIONAL) {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataBidiRemote(), stream_id, BaseConnection::StreamType::ST_BIDIRECTIONAL);
        
    } else {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, BaseConnection::StreamType::ST_RECV);
    }
    streams_map_[stream_id] = new_stream;
    flow_control_->AddRemoteSendData(new_stream->OnFrame(frame));

    // check peer data limit
    if (!flow_control_->CheckRemoteSendDataLimit(send_frame)) {
        InnerConnectionClose(QEC_FLOW_CONTROL_ERROR, frame->GetType(), "flow control stream data limit.");
        return false;
    }
    if (send_frame) {
        frames_list_.push_back(send_frame);
    }

    return true;
}

bool BaseConnection::OnCryptoFrame(std::shared_ptr<IFrame> frame) {
    connection_crypto_.OnCryptoFrame(frame);
    return true;
}

bool BaseConnection::OnNewTokenFrame(std::shared_ptr<IFrame> frame) {
    auto token_frame = std::dynamic_pointer_cast<NewTokenFrame>(frame);
    if (!token_frame) {
        common::LOG_ERROR("invalid new token frame.");
        return false;
    }
    auto data = token_frame->GetToken();
    token_ = std::move(std::string((const char*)data, token_frame->GetTokenLength()));
    return true;
}

bool BaseConnection::OnMaxDataFrame(std::shared_ptr<IFrame> frame) {
    auto max_data_frame = std::dynamic_pointer_cast<MaxDataFrame>(frame);
    if (!max_data_frame) {
        common::LOG_ERROR("invalid max data frame.");
        return false;
    }
    uint64_t max_data_size = max_data_frame->GetMaximumData();
    flow_control_->UpdateLocalSendDataLimit(max_data_size);
    return true;
}

bool BaseConnection::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_->CheckRemoteSendDataLimit(send_frame);
    if (send_frame) {
        frames_list_.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_->CheckRemoteStreamLimit(0, send_frame);
    if (send_frame) {
        frames_list_.push_back(send_frame);
    }
    return true;
}

bool BaseConnection::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);
    if (stream_block_frame->GetType() == FT_MAX_STREAMS_BIDIRECTIONAL) {
        flow_control_->UpdateLocalBidirectionStreamLimit(stream_block_frame->GetMaximumStreams());

    } else {
        flow_control_->UpdateLocalUnidirectionStreamLimit(stream_block_frame->GetMaximumStreams());
    }
    return true;
}

bool BaseConnection::OnNewConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto new_cid_frame = std::dynamic_pointer_cast<NewConnectionIDFrame>(frame);
    if (!new_cid_frame) {
        common::LOG_ERROR("invalid new connection id frame.");
        return false;
    }
    
    remote_conn_id_manager_->RetireIDBySequence(new_cid_frame->GetRetirePriorTo());
    ConnectionID id;
    new_cid_frame->GetConnectionID(id.id_, id.len_);
    id.index_ = new_cid_frame->GetSequenceNumber();
    remote_conn_id_manager_->AddID(id);
    return true;
}

bool BaseConnection::OnRetireConnectionIDFrame(std::shared_ptr<IFrame> frame) {
    auto retire_cid_frame = std::dynamic_pointer_cast<RetireConnectionIDFrame>(frame);
    remote_conn_id_manager_->RetireIDBySequence(retire_cid_frame->GetSequenceNumber());
    return true;
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    send_manager_.AddActiveStream(stream);
    ActiveSend();
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string resion) {
    to_close_ = true;
    // make connection close frame
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    frame->SetErrFrameType(tigger_frame);
    frame->SetReason(resion);
    frames_list_.push_back(frame);
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // remove stream
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        streams_map_.erase(stream);
    }
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    transport_param_.Merge(remote_tp);
    flow_control_->InitConfig(transport_param_);
}

void BaseConnection::AddConnectionId(uint64_t cid_hash) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(cid_hash, shared_from_this());
    }
}

bool BaseConnection::OnNormalPacket(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<ICryptographer> cryptographer = connection_crypto_.GetCryptographer(packet->GetCryptoLevel());
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
    if (!is_active_send_) {
        is_active_send_ = true;
    }
    if (active_connection_cb_) {
        active_connection_cb_(shared_from_this());
    }
}

std::shared_ptr<IStream> BaseConnection::MakeStream(uint32_t init_size, uint64_t stream_id, StreamType st) {
    std::shared_ptr<IStream> new_stream;
    if (st == BaseConnection::ST_BIDIRECTIONAL) {
        new_stream = std::make_shared<BidirectionStream>(alloter_, init_size, stream_id);

    } else if (st == BaseConnection::ST_SEND) {
        new_stream = std::make_shared<SendStream>(alloter_, init_size, stream_id);

    } else {
        new_stream = std::make_shared<RecvStream>(alloter_, init_size, stream_id);
    }
    new_stream->SetStreamCloseCB(std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1));
    new_stream->SetConnectionCloseCB(std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    new_stream->SetActiveStreamSendCB(std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1));
    return new_stream;
}

}
}