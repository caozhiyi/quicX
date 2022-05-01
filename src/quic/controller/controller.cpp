#include <functional>

#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"
#include "quic/udp/udp_listener.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/controller/controller.h"
#include "quic/packet/packet_interface.h"
#include "common/buffer/buffer_interface.h"

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
    udp_packet->Decode(packets);
}

}
