#include <gtest/gtest.h>
#include "quic/connection/type.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_decode.h"
#include "quic/process/server_processor.h"
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/crypto/tls/tls_server_ctx.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace {

bool ConnectionProcess(std::shared_ptr<IConnection> conn, std::shared_ptr<IBuffer> buffer) {
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets)) {
        return false;
    }

    conn->OnPackets(packets);

    buffer->Clear();
    conn->GenerateSendData(buffer);

    return true;
}

TEST(connnection_utest, client) {
    std::shared_ptr<quicx::TLSCtx> client_ctx = std::make_shared<quicx::TLSClientCtx>();
    client_ctx->Init();

    std::shared_ptr<quicx::TLSCtx> server_ctx = std::make_shared<quicx::TLSServerCtx>();
    server_ctx->Init();

    std::shared_ptr<ClientConnection> client_conn = std::make_shared<ClientConnection>(client_ctx);
    client_conn->AddAlpn(AT_HTTP3);

    client_conn->AddTransportParam(TransportParamConfig::Instance());

    Address addr(AT_IPV4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr);

    uint8_t buf[1500] = {0};
    std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(buf, buf + 1500);
    client_conn->GenerateSendData(buffer);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx);
    int times = 5;
    while (times--) {
        ConnectionProcess(server_conn, buffer);
        ConnectionProcess(client_conn, buffer);
    }

}

}
}