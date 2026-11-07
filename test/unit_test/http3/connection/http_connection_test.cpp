#include <quicx/http3/if_response.h>
#include <quicx/http3/type.h>

#include <gtest/gtest.h>

#include "http3/connection/connection_client.h"
#include "http3/connection/connection_server.h"
#include "http3/http/error.h"
#include "http3/http/request.h"

#include "test/unit_test/http3/connection/mock_quic_connection.h"

namespace quicx {
namespace http3 {
namespace {

class MockClient {
public:
    MockClient(std::shared_ptr<IQuicConnection> conn):
        error_code_(0) {
        conn_ = std::make_shared<ClientConnection>("", kDefaultHttp3Settings, conn,
            std::bind(&MockClient::ErrorHandler, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&MockClient::PushPromiseHandler, this, std::placeholders::_1),
            std::bind(&MockClient::PushHandler, this, std::placeholders::_1, std::placeholders::_2));
    }

    void ErrorHandler(const std::string& unique_id, uint32_t error_code) {
        error_code_ = error_code;
        last_error_unique_id_ = unique_id;
    }
    bool PushPromiseHandler(quicx::HttpFields& /*headers*/) {
        // TODO: implement this
        return true;
    }
    void PushHandler(std::shared_ptr<IResponse> /*response*/, uint32_t /*error*/) {
        // TODO: implement this
    }
    // send request
    bool DoRequest(std::shared_ptr<IRequest> request, const http_response_handler& handler) {
        return conn_->DoRequest(request, handler);
    }
    void SetMaxPushID(uint64_t max_push_id) { conn_->SetMaxPushID(max_push_id); }
    void CancelPush(uint64_t push_id) { conn_->CancelPush(push_id); }
    void Shutdown() { conn_->Shutdown(); }
    bool IsAcceptingNewRequests() const { return conn_->IsAcceptingNewRequests(); }
    uint32_t GetErrorCode() const { return error_code_; }
    const std::string& GetLastErrorUniqueId() const { return last_error_unique_id_; }

    // P1-H1 dispatch tests drive the error dispatch entry point directly.
    std::shared_ptr<ClientConnection> GetConn() const { return conn_; }

private:
    uint32_t error_code_;
    std::string last_error_unique_id_;
    std::shared_ptr<ClientConnection> conn_;
};

class MockServer {
public:
    MockServer(std::shared_ptr<IQuicConnection> conn):
        error_code_(0) {
        // Create a mock http processor
        class MockHttpProcessor: public IHttpProcessor {
        public:
            MockHttpProcessor(MockServer* server):
                server_(server) {}
            RouteConfig MatchRoute(
                HttpMethod method, const std::string& path, std::shared_ptr<IRequest> /*request*/ = nullptr) override {
                return server_->MatchHandler(method, path);
            }
            bool BeforeHandlerProcess(std::shared_ptr<IRequest> /*req*/, std::shared_ptr<IResponse> /*resp*/) override { return true; }
            void AfterHandlerProcess(std::shared_ptr<IRequest> /*req*/, std::shared_ptr<IResponse> /*resp*/) override {}

        private:
            MockServer* server_;
        };

        auto processor = std::make_shared<MockHttpProcessor>(this);
        conn_ = std::make_shared<ServerConnection>("", kDefaultHttp3Settings, processor, nullptr, conn,
            std::bind(&MockServer::ErrorHandler, this, std::placeholders::_1, std::placeholders::_2));
    }

    void ErrorHandler(const std::string& unique_id, uint32_t error_code) {
        error_code_ = error_code;
        last_error_unique_id_ = unique_id;
    }
    uint32_t GetErrorCode() const { return error_code_; }
    const std::string& GetLastErrorUniqueId() const { return last_error_unique_id_; }

    RouteConfig MatchHandler(HttpMethod /*method*/, const std::string& /*path*/) {
        // Create wrapper that accesses http_handler_ at call time (not bind time)
        auto wrapper = [this](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
            if (this->http_handler_) {
                this->http_handler_(req, resp);
            } else {
                resp->SetStatusCode(200);
            }
        };
        return RouteConfig(wrapper);
    }

    void SetHttpHandler(const http_handler& handler) { http_handler_ = handler; }

    void Shutdown() { conn_->Shutdown(); }
    bool IsAcceptingNewPushes() const { return conn_->IsAcceptingNewPushes(); }

private:
    uint32_t error_code_;
    std::string last_error_unique_id_;
    std::shared_ptr<ServerConnection> conn_;
    http_handler http_handler_;
};

class HttpConnectionTest: public testing::Test {
protected:
    void SetUp() override {
        mock_conn_1_ = std::make_shared<quic::MockQuicConnection>();
        mock_conn_2_ = std::make_shared<quic::MockQuicConnection>();

        mock_conn_1_->SetPeer(mock_conn_2_);
        mock_conn_2_->SetPeer(mock_conn_1_);

        mock_client_ = std::make_shared<MockClient>(mock_conn_1_);
        mock_server_ = std::make_shared<MockServer>(mock_conn_2_);
    }

    std::shared_ptr<quic::MockQuicConnection> mock_conn_1_;
    std::shared_ptr<quic::MockQuicConnection> mock_conn_2_;
    std::shared_ptr<MockClient> mock_client_;
    std::shared_ptr<MockServer> mock_server_;
};

TEST_F(HttpConnectionTest, DoRequest) {
    std::shared_ptr<IRequest> request = std::make_shared<Request>();
    request->SetMethod(HttpMethod::kGet);
    request->SetPath("/");
    request->SetScheme("http");
    request->SetAuthority("localhost");
    request->AddHeader("User-Agent", "curl/7.64.1");
    request->AddHeader("Accept", "*/*");
    request->AddHeader("Content-Type", "text/plain");
    request->AppendBody("Hello, Server!");

    auto http_handler = [](std::shared_ptr<IRequest> request, std::shared_ptr<IResponse> response) {
        // check headers
        std::string content;
        EXPECT_TRUE(request->GetHeader("Content-Type", content));
        EXPECT_EQ(content, "text/plain");
        EXPECT_TRUE(request->GetHeader("User-Agent", content));
        EXPECT_EQ(content, "curl/7.64.1");
        EXPECT_TRUE(request->GetHeader("Accept", content));
        EXPECT_EQ(content, "*/*");

        // check pseudo headers
        EXPECT_EQ(request->GetMethod(), HttpMethod::kGet);
        EXPECT_EQ(request->GetPath(), "/");
        EXPECT_EQ(request->GetScheme(), "http");
        EXPECT_EQ(request->GetAuthority(), "localhost");

        // check body
        EXPECT_EQ(request->GetBodyAsString(), "Hello, Server!");

        // set response
        response->AddHeader("Content-Type", "text/plain");
        response->AppendBody("Hello, Client!");
        response->SetStatusCode(400);
    };
    mock_server_->SetHttpHandler(http_handler);

    auto http_response_handler = [](std::shared_ptr<IResponse> response, uint32_t /*error*/) {
        // check headers
        std::string content_type;
        EXPECT_TRUE(response->GetHeader("Content-Type", content_type));
        EXPECT_EQ(content_type, "text/plain");

        // check body
        EXPECT_EQ(response->GetBodyAsString(), "Hello, Client!");
        // check status code
        EXPECT_EQ(response->GetStatusCode(), 400);
    };

    EXPECT_TRUE(mock_client_->DoRequest(request, http_response_handler));
}

// RFC 9114 §5.2: after a graceful shutdown is initiated locally, the
// client MUST refuse to start new requests on this connection. We don't
// rely on the GOAWAY frame actually reaching the peer here — the
// MockQuicConnection is intentionally bare-bones — we only validate the
// in-process state transition that gates DoRequest().
TEST_F(HttpConnectionTest, ShutdownRefusesNewRequests) {
    EXPECT_TRUE(mock_client_->IsAcceptingNewRequests());

    mock_client_->Shutdown();

    EXPECT_FALSE(mock_client_->IsAcceptingNewRequests());

    // A request submitted after Shutdown() must be rejected synchronously
    // with H3_REQUEST_REJECTED so the application layer can retry on a
    // fresh connection (RFC 9114 §8.1).
    auto req = std::make_shared<Request>();
    req->SetMethod(HttpMethod::kGet);
    req->SetPath("/");
    req->SetScheme("http");
    req->SetAuthority("localhost");

    bool handler_called = false;
    uint32_t observed_error = 0;
    auto handler = [&handler_called, &observed_error](std::shared_ptr<IResponse> /*resp*/, uint32_t error) {
        handler_called = true;
        observed_error = error;
    };

    EXPECT_FALSE(mock_client_->DoRequest(req, handler));
    EXPECT_TRUE(handler_called);
    EXPECT_EQ(observed_error, static_cast<uint32_t>(Http3ErrorCode::kRequestRejected));
}

// Server-side mirror: once the server begins shutdown, IsAcceptingNewPushes()
// flips false and SendPush() short-circuits before touching any stream
// state. Combined with the client-side test above, this covers both
// directions of the RFC 9114 §5.2 GOAWAY contract for the public API
// surface (Shutdown / IsAcceptingNewRequests / IsAcceptingNewPushes).
TEST_F(HttpConnectionTest, ServerShutdownRefusesNewPushes) {
    EXPECT_TRUE(mock_server_->IsAcceptingNewPushes());

    mock_server_->Shutdown();

    EXPECT_FALSE(mock_server_->IsAcceptingNewPushes());
}

// ---------------------------------------------------------------------------
// P1-H1: RFC 9114 §8 error dispatch (h3spec-style assertions on the on-wire
// behaviour that used to be completely missing: errors stopped at the local
// user callback and the peer never observed anything).
// ---------------------------------------------------------------------------

// The scope table must classify the RFC 9114 §8.1 codes this dispatch relies
// on. Spot-check every branch plus the contextual default.
TEST(Http3ErrorScope, TableClassifiesRfc9114Codes) {
    // Unambiguously connection-level.
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kClosedCriticalStream), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kSettingsError), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kMissingSettings), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kStreamCreationError), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kIdError), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kQpackDecompressionFailed), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kQpackEncoderStreamError), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kQpackDecoderStreamError), Http3ErrorScope::kConnection);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kVersionFallback), Http3ErrorScope::kConnection);

    // Unambiguously request-stream scoped.
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kRequestRejected), Http3ErrorScope::kRequestStream);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kRequestCancelled), Http3ErrorScope::kRequestStream);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kRequestIncomplete), Http3ErrorScope::kRequestStream);

    // Contextual: depends on which stream raised them.
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kFrameUnexpected), Http3ErrorScope::kContextual);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kFrameError), Http3ErrorScope::kContextual);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kMessageError), Http3ErrorScope::kContextual);
    EXPECT_EQ(GetErrorScope(Http3ErrorCode::kInternalError), Http3ErrorScope::kContextual);
}

// A connection-scope code (or an error on a critical stream) MUST close the
// connection carrying the H3 error code: the transport sees
// CONNECTION_CLOSE(error_code) instead of the old silent local cleanup.
TEST_F(HttpConnectionTest, ConnectionLevelErrorClosesConnectionWithH3Code) {
    // kSettingsError on an unknown stream id: scope alone drives the close.
    mock_client_->GetConn()->DispatchH3Error(/*stream_id=*/9999,
        static_cast<uint32_t>(Http3ErrorCode::kSettingsError));

    EXPECT_EQ(mock_conn_1_->GetResetCount(), 1u);
    EXPECT_EQ(mock_conn_1_->GetLastResetErrorCode(), static_cast<uint32_t>(Http3ErrorCode::kSettingsError));
    // The local user callback is still notified.
    EXPECT_EQ(mock_client_->GetErrorCode(), static_cast<uint32_t>(Http3ErrorCode::kSettingsError));
}

// A request-stream scope code on a (non-critical / unknown) stream must NOT
// tear down the whole connection: the transport sees no CONNECTION_CLOSE.
TEST_F(HttpConnectionTest, StreamLevelErrorDoesNotCloseConnection) {
    mock_client_->GetConn()->DispatchH3Error(/*stream_id=*/9999,
        static_cast<uint32_t>(Http3ErrorCode::kRequestCancelled));

    EXPECT_EQ(mock_conn_1_->GetResetCount(), 0u);
    // Local user notification still happens.
    EXPECT_EQ(mock_client_->GetErrorCode(), static_cast<uint32_t>(Http3ErrorCode::kRequestCancelled));
}

// error 0 is normal completion: no transport signalling at all.
TEST_F(HttpConnectionTest, ZeroErrorIsSilentCompletion) {
    mock_client_->GetConn()->DispatchH3Error(/*stream_id=*/9999, 0);
    EXPECT_EQ(mock_conn_1_->GetResetCount(), 0u);
    EXPECT_EQ(mock_client_->GetErrorCode(), 0u);
}


// ---------------------------------------------------------------------------
// Review item 7: a code that is not an HTTP/3 code (a QUIC transport code, or
// the value the PEER chose in its own RESET_STREAM) must never be reflected
// back on the wire. The local callback still sees the original for diagnosis.
// ---------------------------------------------------------------------------

TEST(Http3ErrorScope, KnownCodeTableCoversEveryDefinedCodeAndNothingElse) {
    for (uint32_t code : {0x0100u, 0x0101u, 0x0102u, 0x0103u, 0x0104u, 0x0105u, 0x0106u, 0x0107u, 0x0108u, 0x0109u,
             0x010au, 0x010bu, 0x010cu, 0x010du, 0x010eu, 0x010fu, 0x0110u, 0x0200u, 0x0201u, 0x0202u}) {
        EXPECT_TRUE(IsKnownHttp3ErrorCode(code)) << std::hex << code;
    }
    for (uint32_t code : {0x00u, 0x01u, 0x0au, 0x0111u, 0x01ffu, 0x0203u, 0xdeadbeefu, 0xffffffffu}) {
        EXPECT_FALSE(IsKnownHttp3ErrorCode(code)) << std::hex << code;
    }
}

TEST_F(HttpConnectionTest, UnknownCodeOnRequestStreamKeepsConnectionAndDiagnosticCode) {
    constexpr uint32_t kPeerChosen = 0xdeadbeef;
    mock_client_->GetConn()->DispatchH3Error(/*stream_id=*/9999, kPeerChosen);

    // Mapped to H3_REQUEST_CANCELLED (request scope): the connection survives...
    EXPECT_EQ(mock_conn_1_->GetResetCount(), 0u);
    // ...and the local notification keeps the original value for diagnosis.
    EXPECT_EQ(mock_client_->GetErrorCode(), kPeerChosen);
}

TEST_F(HttpConnectionTest, UnknownCodeOnACriticalStreamClosesWithClosedCriticalStreamNotTheEcho) {
    constexpr uint32_t kPeerChosen = 0xdeadbeef;
    // Init() wires up the control + QPACK streams (two-phase init), which are
    // the critical streams; they take the first stream ids of this connection.
    // Raise the unknown code on each until the connection closes: whichever it
    // is, the wire code must be H3_CLOSED_CRITICAL_STREAM and never the
    // peer-chosen value.
    mock_client_->GetConn()->Init();
    bool closed = false;
    for (uint64_t id = 0; id < 8 && !closed; ++id) {
        mock_client_->GetConn()->DispatchH3Error(id, kPeerChosen);
        closed = mock_conn_1_->GetResetCount() > 0;
    }
    ASSERT_TRUE(closed) << "an error on a critical stream is a connection error";
    EXPECT_EQ(mock_conn_1_->GetLastResetErrorCode(), static_cast<uint32_t>(Http3ErrorCode::kClosedCriticalStream));
    EXPECT_NE(mock_conn_1_->GetLastResetErrorCode(), kPeerChosen);
}

}  // namespace
}  // namespace http3
}  // namespace quicx