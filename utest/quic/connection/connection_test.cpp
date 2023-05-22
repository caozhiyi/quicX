#include <gtest/gtest.h>
#include "quic/connection/type.h"
#include "quic/frame/frame_decode.h"
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/transport_param_config.h"

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

    uint8_t buf[1500] = {0};
    std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(buf, buf + 1500);

    client_conn.GenerateSendData(buffer);
}

}
}