#include <gtest/gtest.h>
#include "quic/connection/type.h"
#include "quic/udp/udp_packet_in.h"
#include "quic/frame/frame_decode.h"
#include "quic/packet/packet_decode.h"
#include "quic/crypto/tls/tls_client_ctx.h"
#include "quic/crypto/tls/tls_server_ctx.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/server_connection.h"
#include "quic/connection/transport_param_config.h"

namespace quicx {
namespace {

static const char __cert_pem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
      "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
      "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
      "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
      "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
      "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
      "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
      "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
      "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
      "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
      "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
      "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
      "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
      "-----END CERTIFICATE-----\n";

static const char __key_pem[] =
      "-----BEGIN RSA PRIVATE KEY-----\n"
      "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
      "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
      "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
      "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
      "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
      "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
      "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
      "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
      "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
      "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
      "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
      "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
      "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
      "-----END RSA PRIVATE KEY-----\n"; 

bool ConnectionProcess(std::shared_ptr<IConnection> conn, std::shared_ptr<IBuffer> buffer) {
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets)) {
        return false;
    }

    conn->OnPackets(0, packets);

    buffer->Clear();
    conn->GenerateSendData(buffer);

    return true;
}

TEST(connnection_utest, handshake) {
    BIO* cert_bio = BIO_new_mem_buf(__cert_pem, strlen(__cert_pem));
    EXPECT_TRUE(cert_bio != nullptr);
    quicx::CSmartPtr<X509, X509_free> cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    EXPECT_TRUE(cert != nullptr);
    BIO_free(cert_bio);

    BIO* key_bio = BIO_new_mem_buf(__key_pem, strlen(__key_pem));
    EXPECT_TRUE(key_bio != nullptr);
    quicx::CSmartPtr<EVP_PKEY, EVP_PKEY_free> key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    EXPECT_TRUE(key != nullptr);
    BIO_free(key_bio);

    std::shared_ptr<quicx::TLSServerCtx> server_ctx = std::make_shared<quicx::TLSServerCtx>();
    server_ctx->Init(cert.get(), key.get());

    std::shared_ptr<quicx::TLSCtx> client_ctx = std::make_shared<quicx::TLSClientCtx>();
    client_ctx->Init();

    std::shared_ptr<ClientConnection> client_conn = std::make_shared<ClientConnection>(client_ctx, nullptr, nullptr, nullptr);
    client_conn->AddAlpn(AT_HTTP3);
    client_conn->AddTransportParam(TransportParamConfig::Instance());

    Address addr(AT_IPV4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr);
    
    uint8_t buf[1500] = {0};
    std::shared_ptr<Buffer> buffer = std::make_shared<Buffer>(buf, buf + 1500);
    client_conn->GenerateSendData(buffer);

    int len = buffer->GetDataLength();

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(TransportParamConfig::Instance());

    int times = 2;
    while (times--) {
        ConnectionProcess(server_conn, buffer);
        ConnectionProcess(client_conn, buffer);
    }

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), EL_APPLICATION);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), EL_APPLICATION);
}

}
}