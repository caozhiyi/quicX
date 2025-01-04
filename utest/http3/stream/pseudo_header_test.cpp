#include <gtest/gtest.h>
#include "http3/http/request.h"
#include "http3/http/response.h"
#include "http3/stream/pseudo-header.h"

namespace quicx {
namespace http3 {

class PseudoHeaderTest : public testing::Test {
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
    request_->SetMethod(HttpMothed::HM_GET);
    request_->SetPath("/test");
    request_->SetScheme("https");
    request_->SetAuthority("example.com");

    PseudoHeader::Instance().EncodeRequest(request_);

    auto headers = request_->GetHeaders();
    EXPECT_EQ(headers[":method"], "GET");
    EXPECT_EQ(headers[":path"], "/test");
    EXPECT_EQ(headers[":scheme"], "https");
    EXPECT_EQ(headers[":authority"], "example.com");
}

TEST_F(PseudoHeaderTest, EncodeRequestPOST) {
    request_->SetMethod(HttpMothed::HM_POST);
    request_->SetPath("/api/data");
    request_->SetScheme("http");
    request_->SetAuthority("api.example.com");
    
    PseudoHeader::Instance().EncodeRequest(request_);

    auto headers = request_->GetHeaders();
    EXPECT_EQ(headers[":method"], "POST");
    EXPECT_EQ(headers[":path"], "/api/data");
    EXPECT_EQ(headers[":scheme"], "http");
    EXPECT_EQ(headers[":authority"], "api.example.com");
}

// Test Request Decoding
TEST_F(PseudoHeaderTest, DecodeRequest) {
    std::unordered_map<std::string, std::string> headers = {
        {":method", "GET"},
        {":path", "/test"},
        {":scheme", "https"},
        {":authority", "example.com"}
    };
    request_->SetHeaders(headers);

    PseudoHeader::Instance().DecodeRequest(request_);

    EXPECT_EQ(request_->GetMethod(), HttpMothed::HM_GET);
    EXPECT_EQ(request_->GetPath(), "/test");
    EXPECT_EQ(request_->GetScheme(), "https");
    EXPECT_EQ(request_->GetAuthority(), "example.com");
}

// Test Response Encoding
TEST_F(PseudoHeaderTest, EncodeResponse) {
    response_->SetStatusCode(200);
    
    PseudoHeader::Instance().EncodeResponse(response_);

    auto headers = response_->GetHeaders();
    EXPECT_EQ(headers[":status"], "200");
}

TEST_F(PseudoHeaderTest, EncodeResponseError) {
    response_->SetStatusCode(404);
    
    PseudoHeader::Instance().EncodeResponse(response_);

    auto headers = response_->GetHeaders();
    EXPECT_EQ(headers[":status"], "404");
}

// Test Response Decoding
TEST_F(PseudoHeaderTest, DecodeResponse) {
    std::unordered_map<std::string, std::string> headers = {
        {":status", "200"}
    };
    response_->SetHeaders(headers);

    PseudoHeader::Instance().DecodeResponse(response_);

    EXPECT_EQ(response_->GetStatusCode(), 200);
}


TEST_F(PseudoHeaderTest, RequestWithCustomHeaders) {
    request_->SetMethod(HttpMothed::HM_POST);
    request_->SetPath("/api/data");
    request_->SetScheme("https");
    request_->SetAuthority("api.example.com");
    request_->AddHeader("content-type", "application/json");
    request_->AddHeader("user-agent", "test-client");

    PseudoHeader::Instance().EncodeRequest(request_);

    auto headers = request_->GetHeaders();
    EXPECT_EQ(headers[":method"], "POST");
    EXPECT_EQ(headers[":path"], "/api/data");
    EXPECT_EQ(headers[":scheme"], "https");
    EXPECT_EQ(headers[":authority"], "api.example.com");
    EXPECT_EQ(headers["content-type"], "application/json");
    EXPECT_EQ(headers["user-agent"], "test-client");
}

TEST_F(PseudoHeaderTest, ResponseWithCustomHeaders) {
    response_->SetStatusCode(201);
    response_->AddHeader("content-type", "application/json");
    response_->AddHeader("server", "test-server");

    PseudoHeader::Instance().EncodeResponse(response_);

    auto headers = response_->GetHeaders();
    EXPECT_EQ(headers[":status"], "201");
    EXPECT_EQ(headers["content-type"], "application/json");
    EXPECT_EQ(headers["server"], "test-server");
}

// Test Encode-Decode Combined Cases
TEST_F(PseudoHeaderTest, RequestEncodeDecodeCombined) {
    // Set initial request values
    request_->SetMethod(HttpMothed::HM_PUT);
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
    EXPECT_EQ(decoded_request->GetMethod(), HttpMothed::HM_PUT);
    EXPECT_EQ(decoded_request->GetPath(), "/api/v1/resource");
    EXPECT_EQ(decoded_request->GetScheme(), "https");
    EXPECT_EQ(decoded_request->GetAuthority(), "api.test.com");
    EXPECT_EQ(decoded_request->GetHeaders()["content-type"], "application/json");
    EXPECT_EQ(decoded_request->GetHeaders()["authorization"], "Bearer token123");
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
    EXPECT_EQ(decoded_response->GetHeaders()["content-type"], "application/json");
    EXPECT_EQ(decoded_response->GetHeaders()["cache-control"], "no-cache");
    EXPECT_EQ(decoded_response->GetHeaders()["x-custom-header"], "custom-value");
}

TEST_F(PseudoHeaderTest, RequestComplexPathEncodeDecodeCombined) {
    // Test with complex path containing query parameters and fragments
    request_->SetMethod(HttpMothed::HM_GET);
    request_->SetPath("/search?q=test&page=1#results");
    request_->SetScheme("https");
    request_->SetAuthority("search.example.com:8443");

    // Encode the request
    PseudoHeader::Instance().EncodeRequest(request_);
    
    // Create a new request for decoding
    auto decoded_request = std::make_shared<Request>();
    decoded_request->SetHeaders(request_->GetHeaders());

    // Decode the request
    PseudoHeader::Instance().DecodeRequest(decoded_request);

    // Verify the complex path is preserved
    EXPECT_EQ(decoded_request->GetMethod(), HttpMothed::HM_GET);
    EXPECT_EQ(decoded_request->GetPath(), "/search?q=test&page=1#results");
    EXPECT_EQ(decoded_request->GetScheme(), "https");
    EXPECT_EQ(decoded_request->GetAuthority(), "search.example.com:8443");
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
    EXPECT_EQ(headers["set-cookie"], "session=123");
    EXPECT_EQ(headers["vary"], "Accept");
}

}  // namespace http3
}  // namespace quicx 