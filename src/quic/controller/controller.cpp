#include <functional>

#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"
#include "common/buffer/buffer_interface.h"

#include "quic/common/constants.h"
#include "quic/udp/udp_listener.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/packet/init_packet.h"
#include "quic/controller/controller.h"
#include "quic/packet/packet_interface.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {

Controller::Controller():
    _listener(nullptr) {
    std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
    LOG_SET(std_log);
    LOG_SET_LEVEL(LL_DEBUG);
}

Controller::~Controller() {

}

bool Controller::SetCrypto(const std::string& ciphers, bool prefer_server_ciphers, const std::string& cert_path, const std::string& key_path, const std::string& key_pwd) {
    /*if (!SSLCtx::Instance().Init()) {
        LOG_ERROR("init ssl ctx failed.");
        return false;
    }
    if (!SSLCtx::Instance().SetCiphers(ciphers, prefer_server_ciphers)) {
        LOG_ERROR("ssl ctx set ciphers failed.");
        return false;
    }
    if (!SSLCtx::Instance().SetCertificateAndKey(cert_path, key_path, key_pwd)) {
        LOG_ERROR("ssl ctx set certificate and key failed.");
        return false;
    }*/
    return true;
}

bool Controller::Listen(const std::string& ip, uint16_t port) {
    _listener = std::make_shared<UdpListener>(std::bind(&Controller::Dispatcher, this, std::placeholders::_1));
    return _listener->Listen(ip, port);
}

bool Controller::Stop() {
    if (_listener) {
        return _listener->Stop();
    }
    return true;
}

void Controller::Dispatcher(std::shared_ptr<IBufferRead> recv_data) {
    auto udp_packet = std::make_shared<UdpPacketIn>(recv_data);

    std::vector<std::shared_ptr<IPacket>> packets;
    if(!udp_packet->Decode(packets)) {
        // todo send version negotiate packet
        return;
    }

    uint8_t* cid = nullptr;
    uint16_t len = 0;
    if (!GetDestConnectionId(packets, cid, len)) {
        LOG_ERROR("get dest connection id failed.");
        return;
    }
    
    // dispatch packet
    long cid_code = ConnectionIDGenerator::Instance().Hash(cid, len);
    auto conn = _conn_map.find(cid_code);
    if (conn != _conn_map.end()) {
        conn->second->HandlePacket(packets);
        return;
    }

    auto new_conn = std::make_shared<ServerConnection>(nullptr);
    _conn_map[cid_code] = new_conn;
    new_conn->HandlePacket(packets);    
}

bool Controller::GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
    if (packets.empty()) {
        LOG_ERROR("parse packet list is empty.");
        return false;
    }
    
    auto first_packet_header = packets[0]->GetHeader();
    if (first_packet_header->GetHeaderType() == PHT_SHORT_HEADER) {
        // todo get short header dcid
    } else {
        auto long_header = dynamic_cast<LongHeader*>(first_packet_header);
        len = long_header->GetDestinationConnectionIdLength();
        cid = (uint8_t*)long_header->GetDestinationConnectionId();
    }
    return true;
}

}
