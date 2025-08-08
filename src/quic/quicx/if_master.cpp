#include "quic/quicx/master.h"

namespace quicx {
namespace quic {

std::shared_ptr<IMaster> IMaster::MakeMaster() {
    return std::make_shared<Master>();
}

}
}
