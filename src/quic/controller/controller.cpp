#include <functional>

#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"
#include "common/buffer/buffer_interface.h"

#include "quic/common/constants.h"
#include "quic/udp/udp_listener.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/packet/long_header.h"
#include "quic/packet/init_packet.h"
#include "quic/controller/controller.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

Controller::Controller():
    _listener(nullptr) {
    std::shared_ptr<Logger> std_log = std::make_shared<StdoutLogger>();
    LOG_SET(std_log);
    LOG_SET_LEVEL(LL_DEBUG);
}

Controller::~Controller() {

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

void Controller::Dispatcher(std::shared_ptr<IBufferReadOnly> recv_data) {
    auto udp_packet = std::make_shared<UdpPacketIn>(recv_data);

    std::vector<std::shared_ptr<IPacket>> packets;
    if(!udp_packet->Decode(packets)) {
        // todo send version negotiate packet
        return;
    }

    // dispatch packet
    for (auto iter = packets.begin(); iter != packets.end(); iter++) {
        switch((*iter)->GetPacketType()) {
        case PT_INITIAL:
            HandleInitial(*iter);
            break;
        case PT_0RTT:
            Handle0rtt(*iter);
            break;
        case PT_HANDSHAKE:
            HandleHandshake(*iter);
            break;
        case PT_RETRY:
            HandleRetry(*iter);
            break;
        case PT_NEGOTIATION:
            HandleNegotiation(*iter);
            break;
        case PT_1RTT:
            Handle1rtt(*iter);
            break;
        default:
            LOG_ERROR("unknow packet type:%d", (*iter)->GetPacketType());
            // todo send version negotiate packet
        }
    }
}


bool Controller::HandleInitial(std::shared_ptr<IPacket> packet) {
    std::shared_ptr<InitPacket> init_packet = std::dynamic_pointer_cast<InitPacket>(packet);
    if (!init_packet) {
        LOG_ERROR("dynamic init packet failed.");
        return false;
    }

    std::shared_ptr<LongHeader> header = std::dynamic_pointer_cast<LongHeader>(packet->GetHeader());
    if (!header) {
        LOG_ERROR("dynamic long header failed.");
        return false;
    }
    
    // check destination connection id length
    if (header->GetDestinationConnectionIdLength() < __min_connection_length) {
        LOG_ERROR("quic too short dcid in initial. len:%d", header->GetDestinationConnectionIdLength());
        return false;
    }
    
    // todo token process

    // create new connection 
    
}

bool Controller::Handle0rtt(std::shared_ptr<IPacket> packet) {

}

bool Controller::HandleHandshake(std::shared_ptr<IPacket> packet) {

}

bool Controller::HandleRetry(std::shared_ptr<IPacket> packet) {

}

bool Controller::HandleNegotiation(std::shared_ptr<IPacket> packet) {

}

bool Controller::Handle1rtt(std::shared_ptr<IPacket> packet) {

}

}
