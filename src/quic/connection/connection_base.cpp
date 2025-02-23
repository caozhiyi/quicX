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
#include "quic/frame/path_response_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/connection/connection_base.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(uint64_t cid_hash, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(uint64_t cid_hash)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    IConnection(timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb),
    last_communicate_time_(0),
    flow_control_(start),
    recv_control_(timer),
    send_manager_(timer),
    state_(ConnectionStateType::kStateConnecting) {

    alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4); // TODO: make it configurable
    connection_crypto_.SetRemoteTransportParamCB(std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    remote_conn_id_manager_ = std::make_shared<ConnectionIDManager>();
    local_conn_id_manager_ = std::make_shared<ConnectionIDManager>(
        std::bind(&BaseConnection::AddConnectionId, this, std::placeholders::_1),
        std::bind(&BaseConnection::RetireConnectionId, this, std::placeholders::_1));

    send_manager_.SetFlowControl(&flow_control_);
    send_manager_.SetRemoteConnectionIDManager(remote_conn_id_manager_);
    send_manager_.SetLocalConnectionIDManager(local_conn_id_manager_);

    recv_control_.SetActiveSendCB(std::bind(&BaseConnection::ActiveSend, this));

    transport_param_.AddTransportParamListener(std::bind(&RecvControl::UpdateConfig, &recv_control_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(std::bind(&SendManager::UpdateConfig, &send_manager_, std::placeholders::_1));
    transport_param_.AddTransportParamListener(std::bind(&FlowControl::UpdateConfig, &flow_control_, std::placeholders::_1));
}

BaseConnection::~BaseConnection() {

}

void BaseConnection::Close() {
    if (state_ != ConnectionStateType::kStateConnected) {
        state_ = ConnectionStateType::kStateDraining;
    }

    // there is no data to send, send connection close frame
    if (send_manager_.GetSendOperation() == SendOperation::kAllSendDone) {
        auto frame = std::make_shared<ConnectionCloseFrame>();
        frame->SetErrorCode(0);
        ToSendFrame(frame);

        // wait closing period
        common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
        timer_->AddTimer(task, send_manager_.GetRtt() * 3, 0); // wait 3 rtt time to close
    }
}

void BaseConnection::Reset(uint32_t error_code) {
    ImmediateClose(error_code, 0, "application reset.");
}

std::shared_ptr<IQuicStream> BaseConnection::MakeStream(StreamDirection type) {
    // check streams limit
    uint64_t stream_id;
    std::shared_ptr<IFrame> frame;
    bool can_make_stream = false;
    if (type == StreamDirection::kSend) {
        can_make_stream = flow_control_.CheckLocalUnidirectionStreamLimit(stream_id, frame);
    } else {
        can_make_stream = flow_control_.CheckLocalBidirectionStreamLimit(stream_id, frame);
    }
    if (frame) {
        ToSendFrame(frame);
    }

    if (!can_make_stream) {
        return nullptr;
    }

    std::shared_ptr<IStream> new_stream;
    if (type == StreamDirection::kSend) {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, StreamDirection::kSend);
    } else {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataBidiLocal(), stream_id, StreamDirection::kBidi);
    }
    streams_map_[stream_id] = new_stream;
    return new_stream;
}

void BaseConnection::AddTransportParam(const QuicTransportParams& tp_config) {
    transport_param_.Init(tp_config);

    // set transport param. TODO define tp length
    uint8_t tp_data[1024] = {0};
    std::shared_ptr<common::Buffer> buf = std::make_shared<common::Buffer>(tp_data, sizeof(tp_data));
    transport_param_.Encode(buf);
    tls_connection_->AddTransportParam(buf->GetData(), buf->GetDataLength());
}

uint64_t BaseConnection::GetConnectionIDHash() {
    return local_conn_id_manager_->GetCurrentID().Hash();
}

bool BaseConnection::GenerateSendData(std::shared_ptr<common::IBuffer> buffer, SendOperation& send_operation) {
    // make quic packet
    uint8_t encrypto_level = GetCurEncryptionLevel();
    auto crypto_grapher = connection_crypto_.GetCryptographer(encrypto_level);
    if (!crypto_grapher) {
        common::LOG_ERROR("encrypt grapher is not ready.");
        return false;
    }

    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), CryptoLevel2PacketNumberSpace(encrypto_level));
    if (ack_frame) {
        send_manager_.ToSendFrame(ack_frame);
    }
    
    bool ret = send_manager_.GetSendData(buffer, encrypto_level, crypto_grapher);
    if (!ret) {
        common::LOG_ERROR("get send data failed.");
    }
    send_operation = send_manager_.GetSendOperation();
    if (send_operation == SendOperation::kAllSendDone && state_ == ConnectionStateType::kStateDraining) {
        state_ = ConnectionStateType::kStateClosed;
        auto frame = std::make_shared<ConnectionCloseFrame>();
        frame->SetErrorCode(0);
        ToSendFrame(frame);

        // wait closing period
        common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
        timer_->AddTimer(task, send_manager_.GetRtt() * 3, 0); // wait 3 rtt time to close
    }
    return ret;
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    for (size_t i = 0; i < packets.size(); i++) {
        recv_control_.OnPacketRecv(now, packets[i]);
        common::LOG_DEBUG("get packet. type:%d", packets[i]->GetHeader()->GetPacketType());
        switch (packets[i]->GetHeader()->GetPacketType())
        {
        case PacketType::kInitialPacketType:
            if (!OnInitialPacket(std::dynamic_pointer_cast<InitPacket>(packets[i]))) {
                common::LOG_ERROR("init packet handle failed.");
            }
            break;
        case PacketType::k0RttPacketType:
            if (!On0rttPacket(std::dynamic_pointer_cast<Rtt0Packet>(packets[i]))) {
                common::LOG_ERROR("0 rtt packet handle failed.");
            }
            break;
        case PacketType::kHandshakePacketType:
            if (!OnHandshakePacket(std::dynamic_pointer_cast<HandshakePacket>(packets[i]))) {
                common::LOG_ERROR("handshakee packet handle failed.");
            }
            break;
        case PacketType::kRetryPacketType:
            if (!OnRetryPacket(std::dynamic_pointer_cast<RetryPacket>(packets[i]))) {
                common::LOG_ERROR("retry packet handle failed.");
            }
            break;
        case PacketType::k1RttPacketType:
            if (!On1rttPacket(std::dynamic_pointer_cast<Rtt1Packet>(packets[i]))) {
                common::LOG_ERROR("1 rtt packet handle failed.");
            }
            break;
        default:
            common::LOG_ERROR("unknow packet type. type:%d", packets[i]->GetHeader()->GetPacketType());
            break;
        }
    }

    // reset idle timeout timer task
    timer_->RmTimer(idle_timeout_task_);
    timer_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0); 
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

bool BaseConnection::On1rttPacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
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

bool BaseConnection::OnHandshakePacket(std::shared_ptr<IPacket> packet) {
    return OnNormalPacket(packet);
}

bool BaseConnection::OnFrames(std::vector<std::shared_ptr<IFrame>>& frames, uint16_t crypto_level) {
    for (size_t i = 0; i < frames.size(); i++) {
        uint16_t type = frames[i]->GetType();
        common::LOG_DEBUG("get frame type: %s", FrameType2String(type).c_str());
        switch (type) {
        case FrameType::kPadding:
            // do nothing
            break;
        case FrameType::kPing: 
            last_communicate_time_ = common::UTCTimeMsec();
            break;
        case FrameType::kAck:
        case FrameType::kAckEcn:
            return OnAckFrame(frames[i], crypto_level);
        case FrameType::kCrypto:
            return OnCryptoFrame(frames[i]);
        case FrameType::kNewToken:
            return OnNewTokenFrame(frames[i]);
        case FrameType::kMaxData:
            return OnMaxDataFrame(frames[i]);
        case FrameType::kMaxStreamsBidirectional:
        case FrameType::kMaxStreamsUnidirectional:
            return OnMaxStreamFrame(frames[i]);
        case FrameType::kDataBlocked: 
            return OnDataBlockFrame(frames[i]);
        case FrameType::kStreamsBlockedBidirectional:
        case FrameType::kStreamsBlockedUnidirectional:
            return OnStreamBlockFrame(frames[i]);
        case FrameType::kNewConnectionId: 
            return OnNewConnectionIDFrame(frames[i]);
        case FrameType::kRetireConnectionId:
            return OnRetireConnectionIDFrame(frames[i]);
        case FrameType::kPathChallenge: 
            return OnPathChallengeFrame(frames[i]);
        case FrameType::kPathResponse: 
            return OnPathResponseFrame(frames[i]);
        case FrameType::kConnectionClose:
            return OnConnectionCloseFrame(frames[i]);
        case FrameType::kConnectionCloseApp:
            return OnConnectionCloseAppFrame(frames[i]);
        case FrameType::kHandshakeDone:
            return OnHandshakeDoneFrame(frames[i]);
        // ********** stream frame **********
        case FrameType::kResetStream:
        case FrameType::kStopSending:
        case FrameType::kStreamDataBlocked:
        case FrameType::kMaxStreamData:
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

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    if (state_ != ConnectionStateType::kStateConnected) {
        return false;
    }

    auto stream_frame = std::dynamic_pointer_cast<StreamFrame>(frame);
    
    // find stream
    uint64_t stream_id = stream_frame->GetStreamID();
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        flow_control_.AddRemoteSendData(stream->second->OnFrame(frame));
        return true;
    }

    // check streams limit    
    std::shared_ptr<IFrame> send_frame;
    bool can_make_stream = flow_control_.CheckRemoteStreamLimit(stream_id, send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    if (!can_make_stream) {
        return false;
    }
    
    // create new stream
    std::shared_ptr<IStream> new_stream;
    if (StreamIDGenerator::GetStreamDirection(stream_id) == StreamIDGenerator::StreamDirection::kBidirectional) {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataBidiRemote(), stream_id, StreamDirection::kBidi);
        
    } else {
        new_stream = MakeStream(transport_param_.GetInitialMaxStreamDataUni(), stream_id, StreamDirection::kRecv);
    }
    // check peer data limit
    if (!flow_control_.CheckRemoteSendDataLimit(send_frame)) {
        InnerConnectionClose(QuicErrorCode::kFlowControlError, frame->GetType(), "flow control stream data limit.");
        return false;
    }
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    // notify stream state
    if (stream_state_cb_) {
        stream_state_cb_(new_stream, 0);
    }
    // new stream process frame
    streams_map_[stream_id] = new_stream;
    flow_control_.AddRemoteSendData(new_stream->OnFrame(frame));
    return true;
}

bool BaseConnection::OnAckFrame(std::shared_ptr<IFrame> frame,  uint16_t crypto_level) {
    auto ns = CryptoLevel2PacketNumberSpace(crypto_level);
    send_manager_.OnPacketAck(ns, frame);
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
    flow_control_.AddLocalSendDataLimit(max_data_size);
    return true;
}

bool BaseConnection::OnDataBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckRemoteSendDataLimit(send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    return true;
}

bool BaseConnection::OnStreamBlockFrame(std::shared_ptr<IFrame> frame) {
    std::shared_ptr<IFrame> send_frame;
    flow_control_.CheckRemoteStreamLimit(0, send_frame);
    if (send_frame) {
        ToSendFrame(send_frame);
    }
    return true;
}

bool BaseConnection::OnMaxStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_block_frame = std::dynamic_pointer_cast<MaxStreamsFrame>(frame);
    if (stream_block_frame->GetType() == FrameType::kMaxStreamsBidirectional) {
        flow_control_.AddLocalBidirectionStreamLimit(stream_block_frame->GetMaximumStreams());

    } else {
        flow_control_.AddLocalUnidirectionStreamLimit(stream_block_frame->GetMaximumStreams());
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

bool BaseConnection::OnConnectionCloseFrame(std::shared_ptr<IFrame> frame) {
    auto close_frame = std::dynamic_pointer_cast<ConnectionCloseFrame>(frame);
    if (!close_frame) {
        common::LOG_ERROR("invalid connection close frame.");
        return false;
    }
    connection_close_cb_(shared_from_this(), close_frame->GetErrorCode(), close_frame->GetReason());
    return true;
}

bool BaseConnection::OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame) {
    auto close_frame = std::dynamic_pointer_cast<ConnectionCloseFrame>(frame);
    if (!close_frame) {
        common::LOG_ERROR("invalid connection close app frame.");
        return false;
    }
    connection_close_cb_(shared_from_this(), close_frame->GetErrorCode(), close_frame->GetReason());
    return true;
}

bool BaseConnection::OnPathChallengeFrame(std::shared_ptr<IFrame> frame) {
    auto challenge_frame = std::dynamic_pointer_cast<PathChallengeFrame>(frame);
    if (!challenge_frame) {
        common::LOG_ERROR("invalid path challenge frame.");
        return false;
    }
    auto data = challenge_frame->GetData();
    auto response_frame = std::make_shared<PathResponseFrame>();
    response_frame->SetData(data);
    ToSendFrame(response_frame);
    return true;
}

bool BaseConnection::OnPathResponseFrame(std::shared_ptr<IFrame> frame) {
    auto response_frame = std::dynamic_pointer_cast<PathResponseFrame>(frame);
    if (!response_frame) {
        common::LOG_ERROR("invalid path response frame.");
        return false;
    }
    auto data = response_frame->GetData();
    if (data) {
        // TODO: check data
    }
    return true;
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    transport_param_.Merge(remote_tp);
    idle_timeout_task_.SetTimeoutCallback(std::bind(&BaseConnection::OnIdleTimeout, this));
    timer_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);
}

void BaseConnection::ThreadTransferBefore() {
    // remove idle timeout timer task from old timer
    timer_->RmTimer(idle_timeout_task_); 
}

void BaseConnection::ThreadTransferAfter() {
    // add idle timeout timer task to new timer
    timer_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);  
}

void BaseConnection::OnIdleTimeout() {
    InnerConnectionClose(QuicErrorCode::kNoError, 0, "idle timeout.");
}

void BaseConnection::OnClosingTimeout() {
    // wait closing period done, notify connection close
    connection_close_cb_(shared_from_this(), QuicErrorCode::kNoError, "normal close.");
    // cancel timers
    timer_->RmTimer(idle_timeout_task_);
}

void BaseConnection::ToSendFrame(std::shared_ptr<IFrame> frame) {
    send_manager_.ToSendFrame(frame);
    ActiveSend();
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    send_manager_.ActiveStream(stream);
    ActiveSend();
}

void BaseConnection::ActiveSend() {
    if (active_connection_cb_) {
        active_connection_cb_(shared_from_this());
    }
}

void BaseConnection::InnerConnectionClose(uint64_t error, uint16_t tigger_frame, std::string reason) {
    if (error != QuicErrorCode::kNoError) {
        ImmediateClose(error, tigger_frame, reason);

    } else {
        Close(); 
    }
}

void BaseConnection::ImmediateClose(uint64_t error, uint16_t tigger_frame, std::string reason) {
    if (state_ == ConnectionStateType::kStateConnected) { 
        state_ = ConnectionStateType::kStateClosed;
    }

    // cancel all streams
    for (auto& stream : streams_map_) {
        stream.second->Reset(error);
    }

    // send connection close frame
    auto frame = std::make_shared<ConnectionCloseFrame>();
    frame->SetErrorCode(error);
    frame->SetErrFrameType(tigger_frame);
    frame->SetReason(reason);
    ToSendFrame(frame);

    // wait closing period
    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    timer_->AddTimer(task, send_manager_.GetRtt(), 0); // wait 1 rtt time to close
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // remove stream
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        streams_map_.erase(stream);
    }
}

void BaseConnection::AddConnectionId(uint64_t cid_hash) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(cid_hash, shared_from_this());
    }
}

void BaseConnection::RetireConnectionId(uint64_t cid_hash) {
    if (retire_conn_id_cb_) {
        retire_conn_id_cb_(cid_hash);
    }
}

std::shared_ptr<IStream> BaseConnection::MakeStream(uint32_t init_size, uint64_t stream_id, StreamDirection sd) {
    std::shared_ptr<IStream> new_stream;
    if (sd == StreamDirection::kBidi) {
        new_stream = std::make_shared<BidirectionStream>(alloter_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    } else if (sd == StreamDirection::kSend) {
        new_stream = std::make_shared<SendStream>(alloter_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    } else {
        new_stream = std::make_shared<RecvStream>(alloter_, init_size, stream_id,
            std::bind(&BaseConnection::ActiveSendStream, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerStreamClose, this, std::placeholders::_1),
            std::bind(&BaseConnection::InnerConnectionClose, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    return new_stream;
}

}
}