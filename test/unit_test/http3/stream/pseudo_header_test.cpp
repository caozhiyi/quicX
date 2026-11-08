#include <gtest/gtest.h>

#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/stream/pseudo_header.h"

namespace quicx {
namespace http3 {

// Headers are an ordered field-line sequence now (RFC 9110 §5.3); this keeps
// the old map-style assertions readable.
static std::string* FindValue(const HttpFields& fields, const std::string& name) {
    static std::string kEmpty;
    for (const auto& field : fields) {
        if (field.first == name) return const_cast<std::string*>(&field.second);
    }
    return &kEmpty;
}

class PseudoHeaderTest: public testing::Test {
protected:
    void SetUp() override {
        request_ = std::make_shared<Request>();
        response_ = std::make_shared<Response>();
    }

    std::shared_ptr<Request> request_;
    std::shared_ptr<Response> response_;
};

// Test Request Encoding
TEST_F(PseudoHeaderTest, EncodeRequestGET) {
    request_->SetMethod(HttpMethod::kGet);
    request_->SetPath("/test");
    request_->SetScheme("https");
    request_->SetAuthority("example.com");

    PseudoHeader::Instance().EncodeRequest(request_);

    const auto& headers = request_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":method"), "GET");
    EXPECT_EQ(*FindValue(headers, ":path"), "/test");
    EXPECT_EQ(*FindValue(headers, ":scheme"), "https");
    EXPECT_EQ(*FindValue(headers, ":authority"), "example.com");
}

TEST_F(PseudoHeaderTest, EncodeRequestPOST) {
    request_->SetMethod(HttpMethod::kPost);
    request_->SetPath("/api/data");
    request_->SetScheme("http");
    request_->SetAuthority("api.example.com");

    PseudoHeader::Instance().EncodeRequest(request_);

    const auto& headers = request_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":method"), "POST");
    EXPECT_EQ(*FindValue(headers, ":path"), "/api/data");
    EXPECT_EQ(*FindValue(headers, ":scheme"), "http");
    EXPECT_EQ(*FindValue(headers, ":authority"), "api.example.com");
}

// Test Request Decoding
TEST_F(PseudoHeaderTest, DecodeRequest) {
    quicx::HttpFields headers = {
        {":method", "GET"}, {":path", "/test"}, {":scheme", "https"}, {":authority", "example.com"}};
    request_->SetHeaders(headers);

    PseudoHeader::Instance().DecodeRequest(request_);

    EXPECT_EQ(request_->GetMethod(), HttpMethod::kGet);
    EXPECT_EQ(request_->GetPath(), "/test");
    EXPECT_EQ(request_->GetScheme(), "https");
    EXPECT_EQ(request_->GetAuthority(), "example.com");
}

// Test Response Encoding
TEST_F(PseudoHeaderTest, EncodeResponse) {
    response_->SetStatusCode(200);

    PseudoHeader::Instance().EncodeResponse(response_);

    const auto& headers = response_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":status"), "200");
}

TEST_F(PseudoHeaderTest, EncodeResponseError) {
    response_->SetStatusCode(404);

    PseudoHeader::Instance().EncodeResponse(response_);

    const auto& headers = response_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":status"), "404");
}

// Test Response Decoding
TEST_F(PseudoHeaderTest, DecodeResponse) {
    quicx::HttpFields headers = {{":status", "200"}};
    response_->SetHeaders(headers);

    PseudoHeader::Instance().DecodeResponse(response_);

    EXPECT_EQ(response_->GetStatusCode(), 200);
}

TEST_F(PseudoHeaderTest, RequestWithCustomHeaders) {
    request_->SetMethod(HttpMethod::kPost);
    request_->SetPath("/api/data");
    request_->SetScheme("https");
    request_->SetAuthority("api.example.com");
    request_->AddHeader("content-type", "application/json");
    request_->AddHeader("user-agent", "test-client");

    PseudoHeader::Instance().EncodeRequest(request_);

    const auto& headers = request_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":method"), "POST");
    EXPECT_EQ(*FindValue(headers, ":path"), "/api/data");
    EXPECT_EQ(*FindValue(headers, ":scheme"), "https");
    EXPECT_EQ(*FindValue(headers, ":authority"), "api.example.com");
    EXPECT_EQ(*FindValue(headers, "content-type"), "application/json");
    EXPECT_EQ(*FindValue(headers, "user-agent"), "test-client");
}

TEST_F(PseudoHeaderTest, ResponseWithCustomHeaders) {
    response_->SetStatusCode(201);
    response_->AddHeader("content-type", "application/json");
    response_->AddHeader("server", "test-server");

    PseudoHeader::Instance().EncodeResponse(response_);

    const auto& headers = response_->GetHeaders();
    EXPECT_EQ(*FindValue(headers, ":status"), "201");
    EXPECT_EQ(*FindValue(headers, "content-type"), "application/json");
    EXPECT_EQ(*FindValue(headers, "server"), "test-server");
}

// Test Encode-Decode Combined Cases
TEST_F(PseudoHeaderTest, RequestEncodeDecodeCombined) {
    // Set initial request values
    request_->SetMethod(HttpMethod::kPut);
    request_->SetPath("/api/v1/resource");
    request_->SetScheme("https");
    request_->SetAuthority("api.test.com");
    request_->AddHeader("content-type", "application/json");
    request_->AddHeader("authorization", "Bearer token123");

    // Encode the request
    PseudoHeader::Instance().EncodeRequest(request_);

    // Create a new request for decoding
    auto decoded_request = std::make_shared<Request>();
    decoded_request->SetHeaders(request_->GetHeaders());

    // Decode the request
    PseudoHeader::Instance().DecodeRequest(decoded_request);

    // Verify all fields match
    EXPECT_EQ(decoded_request->GetMethod(), HttpMethod::kPut);
    EXPECT_EQ(decoded_request->GetPath(), "/api/v1/resource");
    EXPECT_EQ(decoded_request->GetScheme(), "https");
    EXPECT_EQ(decoded_request->GetAuthority(), "api.test.com");
    EXPECT_EQ(*FindValue(decoded_request->GetHeaders(), "content-type"), "application/json");
    EXPECT_EQ(*FindValue(decoded_request->GetHeaders(), "authorization"), "Bearer token123");
}

TEST_F(PseudoHeaderTest, ResponseEncodeDecodeCombined) {
    // Set initial response values
    response_->SetStatusCode(201);
    response_->AddHeader("content-type", "application/json");
    response_->AddHeader("cache-control", "no-cache");
    response_->AddHeader("x-custom-header", "custom-value");

    // Encode the response
    PseudoHeader::Instance().EncodeResponse(response_);

    // Create a new response for decoding
    auto decoded_response = std::make_shared<Response>();
    decoded_response->SetHeaders(response_->GetHeaders());

    // Decode the response
    PseudoHeader::Instance().DecodeResponse(decoded_response);

    // Verify all fields match
    EXPECT_EQ(decoded_response->GetStatusCode(), 201);
    EXPECT_EQ(*FindValue(decoded_response->GetHeaders(), "content-type"), "application/json");
    EXPECT_EQ(*FindValue(decoded_response->GetHeaders(), "cache-control"), "no-cache");
    EXPECT_EQ(*FindValue(decoded_response->GetHeaders(), "x-custom-header"), "custom-value");
}

TEST_F(PseudoHeaderTest, RequestComplexPathEncodeDecodeCombined) {
    // Test with complex path containing query parameters and fragments
    // Per HTTP/3 semantics, DecodeRequest separates path from query params.
    // Fragments (#results) are not sent to the server per RFC 9114, but if
    // present in the :path they end up in the query string parsing.
    request_->SetMethod(HttpMethod::kGet);
    request_->SetPath("/search?q=test&page=1");
    request_->SetScheme("https");
    request_->SetAuthority("search.example.com:8443");

    // Encode the request
    PseudoHeader::Instance().EncodeRequest(request_);

    // Create a new request for decoding
    auto decoded_request = std::make_shared<Request>();
    decoded_request->SetHeaders(request_->GetHeaders());

    // Decode the request
    PseudoHeader::Instance().DecodeRequest(decoded_request);

    // After decoding, path and query params are separated
    EXPECT_EQ(decoded_request->GetMethod(), HttpMethod::kGet);
    EXPECT_EQ(decoded_request->GetPath(), "/search");
    EXPECT_EQ(decoded_request->GetScheme(), "https");
    EXPECT_EQ(decoded_request->GetAuthority(), "search.example.com:8443");

    // Query parameters should be parsed
    auto& query_params = decoded_request->GetQueryParams();
    EXPECT_EQ(query_params.size(), 2);
    EXPECT_EQ(query_params.at("q"), "test");
    EXPECT_EQ(query_params.at("page"), "1");
}

TEST_F(PseudoHeaderTest, ResponseMultipleHeadersEncodeDecodeCombined) {
    // Test with multiple headers of the same type
    response_->SetStatusCode(200);
    response_->AddHeader("set-cookie", "session=123");
    response_->AddHeader("vary", "Accept");

    // Encode the response
    PseudoHeader::Instance().EncodeResponse(response_);

    // Create a new response for decoding
    auto decoded_response = std::make_shared<Response>();
    decoded_response->SetHeaders(response_->GetHeaders());

    // Decode the response
    PseudoHeader::Instance().DecodeResponse(decoded_response);

    // Verify multiple headers are preserved
    EXPECT_EQ(decoded_response->GetStatusCode(), 200);
    auto headers = decoded_response->GetHeaders();
    EXPECT_EQ(*FindValue(headers, "set-cookie"), "session=123");
    EXPECT_EQ(*FindValue(headers, "vary"), "Accept");
}

// ---------------------------------------------------------------------------
// P1-H6a (RFC 9114 §4.3, h3spec-style): a malformed request field section
// MUST be rejected (H3_MESSAGE_ERROR at the caller) instead of silently
// producing a request with missing parts.
// ---------------------------------------------------------------------------

TEST_F(PseudoHeaderTest, MissingMethodPseudoHeaderRejected) {
    request_->SetHeaders({{":path", "/"}, {":scheme", "https"}, {":authority", "a.com"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, DuplicatedPathPseudoHeaderRejected) {
    request_->SetHeaders({{":method", "GET"}, {":scheme", "https"}, {":path", "/"}, {":path", "/x"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, PseudoHeaderAfterRegularFieldRejected) {
    request_->SetHeaders({{"content-type", "text/plain"}, {":method", "GET"}, {":scheme", "https"}, {":path", "/"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, UnknownPseudoHeaderRejected) {
    request_->SetHeaders({{":method", "GET"}, {":scheme", "https"}, {":path", "/"}, {":foo", "bar"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, UppercaseRegularFieldNameRejected) {
    request_->SetHeaders({{":method", "GET"}, {":scheme", "https"}, {":path", "/"}, {"Content-Type", "text/plain"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, ConnectionSpecificFieldRejected) {
    request_->SetHeaders({{":method", "GET"}, {":scheme", "https"}, {":path", "/"}, {"connection", "close"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, WellFormedRequestStillDecodes) {
    request_->SetHeaders({{":method", "GET"}, {":scheme", "https"}, {":path", "/a"}, {":authority", "a.com"},
        {"content-type", "text/plain"}});
    EXPECT_TRUE(PseudoHeader::Instance().DecodeRequest(request_));
    EXPECT_EQ(request_->GetMethod(), HttpMethod::kGet);
    EXPECT_EQ(request_->GetPath(), "/a");
}

// ---------------------------------------------------------------------------
// RFC 9114 4.4 CONNECT: :method + :authority only; :scheme / :path forbidden.
// ---------------------------------------------------------------------------

TEST_F(PseudoHeaderTest, ConnectWithAuthorityOnlyIsAccepted) {
    request_->SetHeaders({{":method", "CONNECT"}, {":authority", "example.com:443"}, {"user-agent", "t"}});
    ASSERT_TRUE(PseudoHeader::Instance().DecodeRequest(request_));
    EXPECT_EQ(request_->GetMethod(), HttpMethod::kConnect);
    EXPECT_EQ(request_->GetAuthority(), "example.com:443");
}

TEST_F(PseudoHeaderTest, ConnectWithSchemeOrPathIsRejected) {
    request_->SetHeaders({{":method", "CONNECT"}, {":scheme", "https"}, {":authority", "a.com:443"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
    request_->SetHeaders({{":method", "CONNECT"}, {":authority", "a.com:443"}, {":path", "/"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, ConnectWithoutOrWithEmptyAuthorityIsRejected) {
    request_->SetHeaders({{":method", "CONNECT"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
    request_->SetHeaders({{":method", "CONNECT"}, {":authority", ""}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, ExtendedConnectProtocolPseudoHeaderStaysRejected) {
    // RFC 9220 needs SETTINGS_ENABLE_CONNECT_PROTOCOL, which is not offered.
    request_->SetHeaders({{":method", "CONNECT"}, {":protocol", "websocket"}, {":scheme", "https"},
        {":path", "/"}, {":authority", "a.com"}});
    EXPECT_FALSE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, EncodeConnectOmitsSchemeAndPath) {
    request_->SetMethod(HttpMethod::kConnect);
    request_->SetAuthority("example.com:443");
    request_->SetPath("/ignored");
    request_->SetScheme("https");
    PseudoHeader::Instance().EncodeRequest(request_);
    bool has_scheme = false, has_path = false, has_authority = false;
    for (const auto& f : request_->GetHeaders()) {
        has_scheme |= (f.first == ":scheme");
        has_path |= (f.first == ":path");
        has_authority |= (f.first == ":authority");
    }
    EXPECT_FALSE(has_scheme);
    EXPECT_FALSE(has_path);
    EXPECT_TRUE(has_authority);
    // And a conforming peer (us) accepts what we produced.
    EXPECT_TRUE(PseudoHeader::Instance().DecodeRequest(request_));
}

TEST_F(PseudoHeaderTest, EncodeNonConnectStillCarriesAllFourPseudoHeaders) {
    request_->SetMethod(HttpMethod::kGet);
    request_->SetAuthority("a.com");
    request_->SetPath("/x");
    request_->SetScheme("https");
    PseudoHeader::Instance().EncodeRequest(request_);
    int pseudo = 0;
    for (const auto& f : request_->GetHeaders()) {
        if (!f.first.empty() && f.first[0] == ':') ++pseudo;
    }
    EXPECT_EQ(pseudo, 4);
}

}  // namespace http3
}  // namespace quicx