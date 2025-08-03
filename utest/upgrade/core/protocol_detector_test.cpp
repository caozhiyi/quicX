#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "upgrade/core/protocol_detector.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {

class ProtocolDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }
};

// Test HTTP/1.1 protocol detection
TEST_F(ProtocolDetectorTest, DetectHTTP1_1) {
    // Test HTTP/1.1 GET request
    std::string http1_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    std::vector<uint8_t> data(http1_request.begin(), http1_request.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

TEST_F(ProtocolDetectorTest, DetectHTTP1_1POST) {
    // Test HTTP/1.1 POST request
    std::string http1_post = "POST /api/data HTTP/1.1\r\nContent-Length: 10\r\n\r\n";
    std::vector<uint8_t> data(http1_post.begin(), http1_post.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

TEST_F(ProtocolDetectorTest, DetectHTTP1_1WithHeaders) {
    // Test HTTP/1.1 with various headers
    std::string http1_headers = 
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Accept: text/html\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    std::vector<uint8_t> data(http1_headers.begin(), http1_headers.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

// Test HTTP/2 protocol detection
TEST_F(ProtocolDetectorTest, DetectHTTP2) {
    // Test HTTP/2 connection preface
    std::string http2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    std::vector<uint8_t> data(http2_preface.begin(), http2_preface.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP2);
}

TEST_F(ProtocolDetectorTest, DetectHTTP2WithSettings) {
    // Test HTTP/2 with settings frame
    std::vector<uint8_t> http2_data = {
        0x00, 0x00, 0x0C, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x64, 0x00, 0x04, 0x00,
        0x00, 0xFF, 0xFF
    };
    
    Protocol detected = ProtocolDetector::Detect(http2_data);
    EXPECT_EQ(detected, Protocol::HTTP2);
}

// Test unknown protocol detection
TEST_F(ProtocolDetectorTest, DetectUnknownProtocol) {
    // Test random data
    std::vector<uint8_t> random_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    Protocol detected = ProtocolDetector::Detect(random_data);
    EXPECT_EQ(detected, Protocol::UNKNOWN);
}

TEST_F(ProtocolDetectorTest, DetectEmptyData) {
    // Test empty data
    std::vector<uint8_t> empty_data;
    
    Protocol detected = ProtocolDetector::Detect(empty_data);
    EXPECT_EQ(detected, Protocol::UNKNOWN);
}

TEST_F(ProtocolDetectorTest, DetectPartialHTTP1_1) {
    // Test partial HTTP/1.1 request (incomplete)
    std::string partial_http1 = "GET / HTTP/1.1\r\nHost:";
    std::vector<uint8_t> data(partial_http1.begin(), partial_http1.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::UNKNOWN);
}

TEST_F(ProtocolDetectorTest, DetectHTTP1_1WithBody) {
    // Test HTTP/1.1 with body
    std::string http1_with_body = 
        "POST /upload HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "Hello";
    std::vector<uint8_t> data(http1_with_body.begin(), http1_with_body.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

TEST_F(ProtocolDetectorTest, DetectHTTP1_1CaseInsensitive) {
    // Test HTTP/1.1 with case variations
    std::string http1_case = "get / http/1.1\r\nhost: example.com\r\n\r\n";
    std::vector<uint8_t> data(http1_case.begin(), http1_case.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

TEST_F(ProtocolDetectorTest, DetectHTTP1_1WithSpaces) {
    // Test HTTP/1.1 with extra spaces
    std::string http1_spaces = "  GET   /   HTTP/1.1  \r\n  Host:  example.com  \r\n  \r\n";
    std::vector<uint8_t> data(http1_spaces.begin(), http1_spaces.end());
    
    Protocol detected = ProtocolDetector::Detect(data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
}

} // namespace upgrade
} // namespace quicx 