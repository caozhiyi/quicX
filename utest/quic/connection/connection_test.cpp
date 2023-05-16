#include <gtest/gtest.h>
#include "quic/connection/type.h"
#include "quic/frame/frame_decode.h"
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/transport_param_config.h"
#include "quic/connection/fix_buffer_packet_visitor.h"

namespace quicx {
namespace {

TEST(connnection_utest, client) {
    std::shared_ptr<quicx::TLSCtx> client_ctx = std::make_shared<quicx::TLSClientCtx>();
    client_ctx->Init();

    ClientConnection client_conn(client_ctx);
    client_conn.AddAlpn(AT_HTTP3);

    client_conn.AddTransportParam(TransportParamConfig::Instance());

    Address addr(AT_IPV4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn.Dial(addr);

    FixBufferPacketVisitor visitor(1456);

    client_conn.TrySendData(&visitor);
    //client_conn.HandlePacket
}

}
}