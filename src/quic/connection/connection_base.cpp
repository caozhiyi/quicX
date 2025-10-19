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
#include "quic/frame/max_data_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/packet/handshake_packet.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/frame/path_response_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/connection/connection_base.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/retire_connection_id_frame.h"

namespace quicx {
namespace quic {

BaseConnection::BaseConnection(StreamIDGenerator::StreamStarter start,
    bool ecn_enabled,
    std::shared_ptr<common::ITimer> timer,
    std::function<void(std::shared_ptr<IConnection>)> active_connection_cb,
    std::function<void(std::shared_ptr<IConnection>)> handshake_done_cb,
    std::function<void(ConnectionID&, std::shared_ptr<IConnection>)> add_conn_id_cb,
    std::function<void(ConnectionID&)> retire_conn_id_cb,
    std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)> connection_close_cb):
    IConnection(timer, active_connection_cb, handshake_done_cb, add_conn_id_cb, retire_conn_id_cb, connection_close_cb),
    ecn_enabled_(ecn_enabled),
    last_communicate_time_(0),
    flow_control_(start),
    recv_control_(timer),
    send_manager_(timer),
    state_(ConnectionStateType::kStateConnecting) {

    alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4); // TODO: make it configurable
    connection_crypto_.SetRemoteTransportParamCB(std::bind(&BaseConnection::OnTransportParams, this, std::placeholders::_1));

    // remote CID manager: on retire, send RETIRE_CONNECTION_ID frame to peer
    remote_conn_id_manager_ = std::make_shared<ConnectionIDManager>(
        [](ConnectionID&){ /* no-op on add for remote CIDs */ },
        [this](ConnectionID& id){
            auto retire = std::make_shared<RetireConnectionIDFrame>();
            retire->SetSequenceNumber(id.GetSequenceNumber());
            ToSendFrame(retire);
        }
    );
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
        return;
    }
    // there is no data to send, send connection close frame
    if (send_manager_.GetSendOperation() == SendOperation::kAllSendDone) {
        state_ = ConnectionStateType::kStateClosed;
        auto frame = std::make_shared<ConnectionCloseFrame>();
        frame->SetErrorCode(0);
        ToSendFrame(frame);

        // wait closing period
        common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
        timer_->AddTimer(task, GetCloseWaitTime() * 3, 0); // wait 3 rtt time to close
        return;
    }

    // there is data to send, send connection close frame later
    state_ = ConnectionStateType::kStateDraining;
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
        // fallback to Initial keys if available (early handshake bootstrap)
        auto init_crypto = connection_crypto_.GetCryptographer(kInitial);
        if (init_crypto) {
            encrypto_level = kInitial;
            crypto_grapher = init_crypto;
        }
    }
    if (!crypto_grapher) {
        common::LOG_ERROR("encrypt grapher is not ready.");
        return false;
    }

    // PATH_CHALLENGE/PATH_RESPONSE frames can only be sent in 1-RTT packets
    // If we're doing path validation and Application keys are ready, use them
    if (path_probe_inflight_ && encrypto_level < kApplication) {
        auto app_crypto = connection_crypto_.GetCryptographer(kApplication);
        if (app_crypto) {
            encrypto_level = kApplication;
            crypto_grapher = app_crypto;
        }
    }

    auto ack_frame = recv_control_.MayGenerateAckFrame(common::UTCTimeMsec(), CryptoLevel2PacketNumberSpace(encrypto_level), ecn_enabled_);
    if (ack_frame) {
        send_manager_.ToSendFrame(ack_frame);
    }
    
    bool ret = send_manager_.GetSendData(buffer, encrypto_level, crypto_grapher);
    if (!ret) {
        common::LOG_WARN("there is no data to send.");
    }

    // Mark Initial packet as sent if we just sent one
    if (encrypto_level == kInitial && buffer->GetDataLength() > 0) {
        initial_packet_sent_ = true;
    }
    
    send_operation = send_manager_.GetSendOperation();
    if (send_operation == SendOperation::kAllSendDone && state_ == ConnectionStateType::kStateDraining) {
        state_ = ConnectionStateType::kStateClosed;
        auto frame = std::make_shared<ConnectionCloseFrame>();
        frame->SetErrorCode(0);
        ToSendFrame(frame);

        // wait closing period
        common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
        timer_->AddTimer(task, GetCloseWaitTime() * 3, 0); // wait 3 rtt time to close
    }
    return ret;
}

void BaseConnection::OnPackets(uint64_t now, std::vector<std::shared_ptr<IPacket>>& packets) {
    // Accumulate ECN to ACK_ECN counters based on first packet number space
    if (!packets.empty() && ecn_enabled_) {
        auto ns = CryptoLevel2PacketNumberSpace(packets[0]->GetCryptoLevel());
        recv_control_.OnEcnCounters(pending_ecn_, ns);
    }
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
    if (!connection_crypto_.InitIsReady()) {
        LongHeader* header = (LongHeader*)packet->GetHeader();
        connection_crypto_.InstallInitSecret((uint8_t*)header->GetDestinationConnectionId(), header->GetDestinationConnectionIdLength(), true);
    }
    return OnNormalPacket(packet);
}

bool BaseConnection::On0rttPacket(std::shared_ptr<IPacket> packet) {
    // Handle 0-RTT packet like normal packet using early-data keys if available
    // If early data is disabled on server, the keys won't be available and decryption will fail
    // This is expected behavior - the packet will be dropped and early data will be rejected during TLS handshake
    return OnNormalPacket(packet);
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
        common::LOG_DEBUG("recv frame: %s", FrameType2String(type).c_str());
        switch (type) {
        case FrameType::kPadding:
            // do nothing
            break;
        case FrameType::kPing: 
            last_communicate_time_ = common::UTCTimeMsec();
            break;
        case FrameType::kAck:
        case FrameType::kAckEcn:
            if (!OnAckFrame(frames[i], crypto_level)) {
                return false;
            }
            break;
        case FrameType::kCrypto:
            if (!OnCryptoFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kNewToken:
            if (!OnNewTokenFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kMaxData:
            if (!OnMaxDataFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kMaxStreamsBidirectional:
        case FrameType::kMaxStreamsUnidirectional:
            if (!OnMaxStreamFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kDataBlocked: 
            if (!OnDataBlockFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kStreamsBlockedBidirectional:
        case FrameType::kStreamsBlockedUnidirectional:
            if (!OnStreamBlockFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kNewConnectionId: 
            if (!OnNewConnectionIDFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kRetireConnectionId:
            if (!OnRetireConnectionIDFrame(frames[i])) { 
                return false;
            }
            break;
        case FrameType::kPathChallenge: 
            if (!OnPathChallengeFrame(frames[i])) { 
                return false;
            }
            break;
        case FrameType::kPathResponse: 
            if (!OnPathResponseFrame(frames[i])) { 
                return false;
            }
            break;
        case FrameType::kConnectionClose:
            if (!OnConnectionCloseFrame(frames[i])) { 
                return false;
            }
            break;
        case FrameType::kConnectionCloseApp:
            if (!OnConnectionCloseAppFrame(frames[i])) {
                return false;
            }
            break;
        case FrameType::kHandshakeDone:
            if (!OnHandshakeDoneFrame(frames[i])) {
                return false;
            }
            break;
        // ********** stream frame **********
        case FrameType::kResetStream:
        case FrameType::kStopSending:
        case FrameType::kStreamDataBlocked:
        case FrameType::kMaxStreamData:
            if (!OnStreamFrame(frames[i])) {
                return false;
            }
            break;
        default:
            if (StreamFrame::IsStreamFrame(type)) {
                if (!OnStreamFrame(frames[i])) { 
                    return false; 
                }
            } else {
                common::LOG_ERROR("invalid frame type. type:%s", type);
                return false;
            }
        }
    }
    return true;
}

bool BaseConnection::OnStreamFrame(std::shared_ptr<IFrame> frame) {
    auto stream_frame = std::dynamic_pointer_cast<IStreamFrame>(frame);
    if (!stream_frame) {
        common::LOG_ERROR("invalid stream frame.");
        return false;
    }
    
    // Allow processing of application data only when encryption is ready; 0-RTT stream frames
    // arrive before handshake confirmation and must be accepted per RFC (subject to anti-replay policy
    // which is handled at TLS/session level). Here we don't gate on connection state.
    common::LOG_DEBUG("process stream data frame. stream id:%d", stream_frame->GetStreamID());
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
    new_cid_frame->GetConnectionID(id);
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

    if (state_ != ConnectionStateType::kStateConnected) {
        return false;
    }
    state_ = ConnectionStateType::kStateClosed;
    
    if (close_frame->GetErrorCode() != QuicErrorCode::kNoError) {
        // wait closing period
        common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
        timer_->AddTimer(task, GetCloseWaitTime(), 0); // wait 1 rtt time to close
        return true;
    }

    // wait closing period
    common::TimerTask task(std::bind(&BaseConnection::OnClosingTimeout, this));
    timer_->AddTimer(task, GetCloseWaitTime() * 3, 0); // wait 3 rtt time to close
    return true;
}

bool BaseConnection::OnConnectionCloseAppFrame(std::shared_ptr<IFrame> frame) {
    return OnConnectionCloseFrame(frame);
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
    if (path_probe_inflight_ && memcmp(data, pending_path_challenge_data_, 8) == 0) {
        // token matched: path validated -> promote candidate to active
        path_probe_inflight_ = false;
        timer_->RmTimer(path_probe_task_); // Cancel retry timer
        memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));
        
        if (!(candidate_peer_addr_ == peer_addr_)) {
            common::LOG_INFO("Path validated successfully, switching from %s:%d to %s:%d", 
                           peer_addr_.GetIp().c_str(), peer_addr_.GetPort(),
                           candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
            
            SetPeerAddress(candidate_peer_addr_);
            // rotate to next remote CID if available
            remote_conn_id_manager_->UseNextID();
            // reset cwnd/RTT and PMTU for new path
            send_manager_.ResetPathSignals();
            send_manager_.ResetMtuForNewPath();
            // kick off a minimal PMTU probe sequence on the new path
            send_manager_.StartMtuProbe();
            ExitAntiAmplification();
        }
        
        // candidate consumed
        candidate_peer_addr_ = common::Address();
        
        // Check and replenish local CID pool after successful migration
        CheckAndReplenishLocalCIDPool();
        
        // Start probing next address in queue if any
        StartNextPathProbe();
    }
    return true;
}

void BaseConnection::OnTransportParams(TransportParam& remote_tp) {
    transport_param_.Merge(remote_tp);
    idle_timeout_task_.SetTimeoutCallback(std::bind(&BaseConnection::OnIdleTimeout, this));
    // TODO: modify idle timer set point
    timer_->AddTimer(idle_timeout_task_, transport_param_.GetMaxIdleTimeout(), 0);

    // Preferred Address Migration (RFC 9000 Section 9.6)
    // 
    // IMPORTANT: This is CLIENT-SIDE ONLY logic for handling server's preferred address.
    // 
    // How it works:
    // 1. SERVER: Advertises a preferred_address in transport parameters during handshake
    //    - This is typically used when the server wants the client to use a different address
    //    - Example: Load balancer forwards initial connection, server wants client to connect directly
    //    - Server sets this via transport_param.SetPreferredAddress("ip:port") before handshake
    // 
    // 2. CLIENT: Receives preferred_address and decides whether to migrate (this code)
    //    - Only if active migration is not disabled
    //    - Only if the preferred address is different from current peer address
    //    - Initiates path validation to the new address
    //    - If validation succeeds, switches to the new address
    // 
    // 3. SERVER: Does NOT actively migrate its own address
    //    - Server continues listening on all its addresses
    //    - Server responds to PATH_CHALLENGE from client on the preferred address
    //    - After client validates, communication happens on the new address
    //
    if (!transport_param_.GetDisableActiveMigration()) {
        const auto& pref = transport_param_.GetPreferredAddress();
        if (!pref.empty()) {
            common::LOG_INFO("Server advertised preferred address: %s", pref.c_str());
            
            // Parse "ip:port" format
            auto pos = pref.find(':');
            if (pos != std::string::npos) {
                common::Address addr(pref.substr(0, pos), static_cast<uint16_t>(std::stoi(pref.substr(pos + 1))));
                if (!(addr == GetPeerAddress())) {
                    common::LOG_INFO("Client initiating migration to server's preferred address: %s:%d",
                                   addr.GetIp().c_str(), addr.GetPort());
                    candidate_peer_addr_ = addr;
                    StartPathValidationProbe();
                } else {
                    common::LOG_DEBUG("Preferred address is same as current address, no migration needed");
                }
            } else {
                common::LOG_WARN("Invalid preferred address format: %s (expected ip:port)", pref.c_str());
            }
        }
    }
    
    // Initialize local CID pool for potential path migrations
    CheckAndReplenishLocalCIDPool();
    
    // Start any deferred path probes now that Application keys should be ready
    // (OnTransportParams is called after handshake completes)
    if (!path_probe_inflight_ && !pending_candidate_addrs_.empty()) {
        common::LOG_INFO("Starting deferred path probe (Application keys now ready)");
        StartNextPathProbe();
    }
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
    // cancel timers
    timer_->RmTimer(idle_timeout_task_);
    // wait closing period done, notify connection close
    connection_close_cb_(shared_from_this(), QuicErrorCode::kNoError, "normal close.");
}

uint32_t BaseConnection::GetCloseWaitTime() {
    uint32_t rtt = send_manager_.GetRtt();
    if (rtt == 0 || rtt > 10000) {
        rtt = 500; // default rtt set 500ms
    }
    return rtt;
}

void BaseConnection::ToSendFrame(std::shared_ptr<IFrame> frame) {
    send_manager_.ToSendFrame(frame);
    ActiveSend();
}

void BaseConnection::StartPathValidationProbe() {
    if (path_probe_inflight_) {
        return;
    }
    
    // PATH_CHALLENGE can only be sent in 1-RTT packets, so Application keys must be ready
    // If not ready yet, the probe will be triggered later when OnTransportParams completes
    if (!connection_crypto_.GetCryptographer(kApplication)) {
        common::LOG_DEBUG("Path validation deferred: Application keys not ready yet");
        // Don't add to pending queue here - caller (OnObservedPeerAddress) already did that
        return;
    }
    
    // generate PATH_CHALLENGE
    auto challenge = std::make_shared<PathChallengeFrame>();
    challenge->MakeData();
    memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
    path_probe_inflight_ = true;
    EnterAntiAmplification();
    // reset anti-amplification budget on send manager
    send_manager_.ResetAmpBudget();
    ToSendFrame(challenge);
    probe_retry_count_ = 0;
    probe_retry_delay_ms_ = 1 * 100; // start with 100ms
    ScheduleProbeRetry();
}

void BaseConnection::StartNextPathProbe() {
    // Check if there are pending addresses to probe
    if (pending_candidate_addrs_.empty()) {
        return;
    }
    
    // Get next address from queue
    candidate_peer_addr_ = pending_candidate_addrs_.front();
    pending_candidate_addrs_.erase(pending_candidate_addrs_.begin());
    
    common::LOG_INFO("Starting next path probe from queue to %s:%d (remaining in queue: %zu)", 
                    candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort(), 
                    pending_candidate_addrs_.size());
    
    StartPathValidationProbe();
}

void BaseConnection::EnterAntiAmplification() {
    // Disable streams while path is unvalidated to limit to probing/ACK frames
    send_manager_.SetStreamsAllowed(false);
}

void BaseConnection::ExitAntiAmplification() {
    send_manager_.SetStreamsAllowed(true);
}

void BaseConnection::ScheduleProbeRetry() {
    timer_->RmTimer(path_probe_task_);
    if (!path_probe_inflight_) return;
    
    if (probe_retry_count_ >= 5) {
        // Give up probing after max retries; revert to old path
        common::LOG_WARN("Path validation failed after %d attempts, reverting to old path. candidate: %s:%d", 
                        probe_retry_count_, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
        
        // Clean up probe state
        path_probe_inflight_ = false;
        candidate_peer_addr_ = common::Address();
        memset(pending_path_challenge_data_, 0, sizeof(pending_path_challenge_data_));
        
        // Critical: restore stream sending capability
        ExitAntiAmplification();
        
        // Start probing next address in queue if any
        StartNextPathProbe();
        return;
    }
    
    probe_retry_count_++;
    probe_retry_delay_ms_ = std::min<uint32_t>(probe_retry_delay_ms_ * 2, 2000);
    path_probe_task_.SetTimeoutCallback([this]() {
        if (!path_probe_inflight_) return;
        auto challenge = std::make_shared<PathChallengeFrame>();
        challenge->MakeData();
        common::LOG_DEBUG("Retrying path validation (attempt %d/%d) to %s:%d", 
                         probe_retry_count_ + 1, 5, candidate_peer_addr_.GetIp().c_str(), candidate_peer_addr_.GetPort());
        memcpy(pending_path_challenge_data_, challenge->GetData(), 8);
        ToSendFrame(challenge);
        ScheduleProbeRetry();
    });
    timer_->AddTimer(path_probe_task_, probe_retry_delay_ms_, 0);
}

void BaseConnection::ActiveSendStream(std::shared_ptr<IStream> stream) {
    if (state_ == ConnectionStateType::kStateClosed || state_ == ConnectionStateType::kStateDraining) {
        return;
    }
    if (stream->GetStreamID() != 0) {
        has_app_send_pending_ = true;
    }
    send_manager_.ActiveStream(stream);
    ActiveSend();
}

EncryptionLevel BaseConnection::GetCurEncryptionLevel() {
    auto level = connection_crypto_.GetCurEncryptionLevel();
    
    // In 0-RTT scenario, we need to ensure proper packet sending order:
    // 1. First send Initial packet (with ClientHello)
    // 2. Then send 0-RTT packet (with early data)
    if (has_app_send_pending_ && level == kInitial) {
        // Check if we have 0-RTT keys available
        if (connection_crypto_.GetCryptographer(kEarlyData)) {
            // Check if we have already sent the Initial packet with ClientHello
            // This ensures we don't skip the Initial packet in 0-RTT scenarios
            if (initial_packet_sent_) {
                return kEarlyData;
            } else {
                // Still need to send Initial packet first
                return kInitial;
            }
        }
    }
    return level;
}

void BaseConnection::OnObservedPeerAddress(const common::Address& addr) {
    if (addr == peer_addr_) {
        return;
    }
    
    common::LOG_INFO("Observed new peer address: %s:%d (current: %s:%d)", 
                     addr.GetIp().c_str(), addr.GetPort(), 
                     peer_addr_.GetIp().c_str(), peer_addr_.GetPort());
    
    // Respect disable_active_migration: ignore proactive migration but allow NAT rebinding
    // Heuristic: if we have received any packet from the new address (workers call
    // OnCandidatePathDatagramReceived before this frame processing), it's likely NAT rebinding.
    if (transport_param_.GetDisableActiveMigration()) {
        // Only consider as NAT rebinding if we see repeated observations; otherwise ignore
        if (!(addr == candidate_peer_addr_)) {
            // First observation: store candidate but do not start probe yet
            common::LOG_DEBUG("First observation of new address (migration disabled), waiting for confirmation");
            candidate_peer_addr_ = addr;
            return;
        }
        // Second consecutive observation of same new address: treat as rebinding and probe
        common::LOG_INFO("Second observation confirmed, treating as NAT rebinding");
    }
    
    // Check if this address is already in the queue or currently being probed
    if (path_probe_inflight_ && addr == candidate_peer_addr_) {
        common::LOG_DEBUG("Address %s:%d is already being probed, ignoring", 
                         addr.GetIp().c_str(), addr.GetPort());
        return;
    }
    
    for (const auto& pending : pending_candidate_addrs_) {
        if (addr == pending) {
            common::LOG_DEBUG("Address %s:%d already in probe queue, ignoring", 
                             addr.GetIp().c_str(), addr.GetPort());
            return;
        }
    }
    
    // If probe is in progress, add to queue; otherwise start immediately
    if (path_probe_inflight_) {
        pending_candidate_addrs_.push_back(addr);
        common::LOG_INFO("Added %s:%d to probe queue (queue size: %zu)", 
                        addr.GetIp().c_str(), addr.GetPort(), pending_candidate_addrs_.size());
    } else {
        candidate_peer_addr_ = addr;
        StartPathValidationProbe();
        
        // If probe didn't start (e.g., Application keys not ready), queue the address for later
        if (!path_probe_inflight_) {
            pending_candidate_addrs_.push_back(addr);
            common::LOG_INFO("Path probe deferred, added %s:%d to queue (queue size: %zu)", 
                            addr.GetIp().c_str(), addr.GetPort(), pending_candidate_addrs_.size());
        } else {
            common::LOG_INFO("Started path validation probe to %s:%d", addr.GetIp().c_str(), addr.GetPort());
        }
    }
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
    if (state_ != ConnectionStateType::kStateConnected) {
        return;
    }
    state_ = ConnectionStateType::kStateClosed;

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
    timer_->AddTimer(task, GetCloseWaitTime(), 0); // wait 1 rtt time to close
}

void BaseConnection::InnerStreamClose(uint64_t stream_id) {
    // remove stream
    auto stream = streams_map_.find(stream_id);
    if (stream != streams_map_.end()) {
        streams_map_.erase(stream);
    }
}

void BaseConnection::AddConnectionId(ConnectionID& id) {
    if (add_conn_id_cb_) {
        add_conn_id_cb_(id, shared_from_this());
    }
}

void BaseConnection::RetireConnectionId(ConnectionID& id) {
    if (retire_conn_id_cb_) {
        retire_conn_id_cb_(id);
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

void BaseConnection::CheckAndReplenishLocalCIDPool() {
    if (!local_conn_id_manager_) {
        return;
    }
    
    size_t current_count = local_conn_id_manager_->GetAvailableIDCount();
    
    // If we have enough CIDs, no need to replenish
    if (current_count >= kMinLocalCIDPoolSize) {
        return;
    }
    
    // Calculate how many CIDs to generate (up to max pool size)
    size_t to_generate = std::min<size_t>(
        kMaxLocalCIDPoolSize - current_count, 
        kMaxLocalCIDPoolSize
    );
    
    common::LOG_INFO("Replenishing local CID pool: current=%zu, generating=%zu", 
                    current_count, to_generate);
    
    for (size_t i = 0; i < to_generate; ++i) {
        // Generate new connection ID
        ConnectionID new_cid = local_conn_id_manager_->Generator();
        
        // Create and send NEW_CONNECTION_ID frame
        auto frame = std::make_shared<NewConnectionIDFrame>();
        frame->SetSequenceNumber(new_cid.GetSequenceNumber());
        frame->SetRetirePriorTo(0); // Don't force retirement of older IDs
        frame->SetConnectionID(const_cast<uint8_t*>(new_cid.GetID()), new_cid.GetLength());
        
        // Generate stateless reset token (using random data for now)
        // In production, this should be derived from a secret
        uint8_t reset_token[16];
        for (int j = 0; j < 16; ++j) {
            reset_token[j] = static_cast<uint8_t>(rand() % 256);
        }
        frame->SetStatelessResetToken(reset_token);
        
        ToSendFrame(frame);
        
        common::LOG_DEBUG("Generated NEW_CONNECTION_ID: seq=%llu, len=%d", 
                         new_cid.GetSequenceNumber(), new_cid.GetLength());
    }
}

}
}