#include "common/log/log.h"
#include "quic/process/processor_interface.h"

namespace quicx {

bool IProcessor::GetDestConnectionId(const std::vector<std::shared_ptr<IPacket>>& packets, uint8_t* &cid, uint16_t& len) {
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