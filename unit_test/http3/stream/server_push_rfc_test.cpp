#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "http3/stream/type.h"
#include "http3/http/response.h"
#include "http3/qpack/qpack_encoder.h"
#include "http3/stream/push_sender_stream.h"
#include "http3/stream/unidentified_stream.h"
#include "http3/stream/push_receiver_stream.h"
#include "unit_test/http3/stream/mock_quic_stream.h"

namespace quicx {
namespace http3 {
namespace {

/**
 * RFC 9114 Server Push Compliance Tests
 * 
 * These tests verify that the implementation conforms to RFC 9114,
 * particularly Section 4.6 (Push Streams) format requirements.
 */

class ServerPushRFCTest : public testing::Test {
protected:
    void SetUp() override {
        qpack_encoder_ = std::make_shared<QpackEncoder>();
        
        mock_send_stream_ = std::make_shared<quic::MockQuicStream>();
        mock_recv_stream_ = std::make_shared<quic::MockQuicStream>();
        
        mock_recv_stream_->SetPeer(mock_send_stream_);
        mock_send_stream_->SetPeer(mock_recv_stream_);
        
        error_code_ = 0;
        push_response_received_ = nullptr;
        
        sender_stream_ = std::make_shared<PushSenderStream>(
            qpack_encoder_,
            mock_send_stream_,
            [this](uint64_t stream_id, uint32_t error) {
                error_code_ = error;
            }
        );
        
        // Start with UnidentifiedStream to read stream type
        unidentified_stream_ = std::make_shared<UnidentifiedStream>(
            mock_recv_stream_,
            [this](uint64_t stream_id, uint32_t error) {
                error_code_ = error;
            },
            [this](uint64_t stream_type, 
                   std::shared_ptr<quic::IQuicRecvStream> stream,
                   std::shared_ptr<common::IBufferRead> remaining_data) {
                this->OnStreamTypeIdentified(stream_type, stream, remaining_data);
            }
        );
    }

    void OnStreamTypeIdentified(uint64_t stream_type, 
                                std::shared_ptr<quic::IQuicRecvStream> stream,
                                std::shared_ptr<common::IBufferRead> remaining_data) {
        // Verify it's a push stream
        EXPECT_EQ(stream_type, static_cast<uint64_t>(StreamType::kPush));
        
        // Create the actual push receiver stream
        receiver_stream_ = std::make_shared<PushReceiverStream>(
            qpack_encoder_,
            stream,
            [this](uint64_t stream_id, uint32_t error) {
                error_code_ = error;
            },
            [this](std::shared_ptr<IResponse> response, uint32_t error) {
                push_response_received_ = response;
                error_code_ = error;
            }
        );
        
        // Feed remaining data to the push stream
        if (remaining_data && remaining_data->GetDataLength() > 0) {
            receiver_stream_->OnData(remaining_data, 0);
        }
    }

    std::shared_ptr<QpackEncoder> qpack_encoder_;
    std::shared_ptr<quic::MockQuicStream> mock_send_stream_;
    std::shared_ptr<quic::MockQuicStream> mock_recv_stream_;
    std::shared_ptr<UnidentifiedStream> unidentified_stream_;
    std::shared_ptr<PushSenderStream> sender_stream_;
    std::shared_ptr<PushReceiverStream> receiver_stream_;
    std::shared_ptr<IResponse> push_response_received_;
    uint32_t error_code_;
};

// Test 1: Basic Push Stream Send/Receive
TEST_F(ServerPushRFCTest, BasicPushStreamFlow) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->SetBody("<html>Pushed content</html>");
    
    uint64_t push_id = 1;
    
    // Send push response (includes stream type, push ID, and HTTP message)
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    
    // Verify receiver got the response
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetStatusCode(), 200);
    EXPECT_EQ(push_response_received_->GetBody(), "<html>Pushed content</html>");
}

// Test 2: Push with Zero ID (RFC 9114 allows Push ID starting from 0)
TEST_F(ServerPushRFCTest, PushIDZeroIsValid) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->SetBody("Content for push ID 0");
    
    uint64_t push_id = 0;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetBody(), "Content for push ID 0");
}

// Test 3: Push with Large ID
TEST_F(ServerPushRFCTest, LargePushID) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->SetBody("Large push ID content");
    
    // Test a large push ID that requires multi-byte varint encoding
    uint64_t push_id = 1000000;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetBody(), "Large push ID content");
}

// Test 4: Push Stream with Headers Only (No Body)
TEST_F(ServerPushRFCTest, PushStreamHeadersOnly) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(204); // No Content
    // No body, no content-length header
    
    uint64_t push_id = 5;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetStatusCode(), 204);
}

// Test 5: Multiple Different Push IDs
TEST_F(ServerPushRFCTest, MultiplePushIDs) {
    std::vector<std::pair<uint64_t, std::string>> test_cases = {
        {1, "First push"},
        {2, "Second push"},
        {10, "Tenth push"},
        {100, "Hundredth push"},
    };
    
    for (const auto& [push_id, body] : test_cases) {
        SetUp(); // Reset for each test case
        
        auto response = std::make_shared<Response>();
        response->SetStatusCode(200);
        response->SetBody(body);
        
        ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response))
            << "Failed to send push with ID: " << push_id;
        
        ASSERT_NE(push_response_received_, nullptr)
            << "No response received for push ID: " << push_id;
        EXPECT_EQ(push_response_received_->GetBody(), body)
            << "Body mismatch for push ID: " << push_id;
    }
}

// Test 6: Push with Various Status Codes
TEST_F(ServerPushRFCTest, VariousStatusCodes) {
    std::vector<uint32_t> status_codes = {200, 201, 204, 301, 304};
    
    for (uint32_t status : status_codes) {
        SetUp(); // Reset
        
        auto response = std::make_shared<Response>();
        response->SetStatusCode(status);
        if (status != 204) {
            response->SetBody("Body for status " + std::to_string(status));
        }
        
        uint64_t push_id = status;
        ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
        ASSERT_NE(push_response_received_, nullptr);
        EXPECT_EQ(push_response_received_->GetStatusCode(), status);
    }
}

// Test 7: Push with Empty Body
TEST_F(ServerPushRFCTest, EmptyBody) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->AddHeader("content-length", "0");
    response->SetBody(""); // Empty body
    
    uint64_t push_id = 15;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetStatusCode(), 200);
    EXPECT_EQ(push_response_received_->GetBody(), "");
}

// Test 8: Push with Multiple Headers
TEST_F(ServerPushRFCTest, MultipleHeaders) {
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->AddHeader("content-type", "application/json");
    response->AddHeader("cache-control", "no-cache");
    response->AddHeader("x-custom-header", "custom-value");
    response->AddHeader("etag", "\"xyz789\"");
    response->SetBody("{\"data\":\"test\"}");
    
    uint64_t push_id = 20;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    
    std::string content_type, cache_control, custom, etag;
    EXPECT_TRUE(push_response_received_->GetHeader("content-type", content_type));
    EXPECT_TRUE(push_response_received_->GetHeader("cache-control", cache_control));
    EXPECT_TRUE(push_response_received_->GetHeader("x-custom-header", custom));
    EXPECT_TRUE(push_response_received_->GetHeader("etag", etag));
    
    EXPECT_EQ(content_type, "application/json");
    EXPECT_EQ(cache_control, "no-cache");
    EXPECT_EQ(custom, "custom-value");
    EXPECT_EQ(etag, "\"xyz789\"");
}

// Test 9: Sequential Push IDs (RFC 9114: Push IDs should be sequential)
TEST_F(ServerPushRFCTest, SequentialPushIDs) {
    for (uint64_t push_id = 0; push_id < 5; ++push_id) {
        SetUp(); // Reset
        
        auto response = std::make_shared<Response>();
        response->SetStatusCode(200);
        response->SetBody("Push " + std::to_string(push_id));
        
        ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response))
            << "Failed at push_id: " << push_id;
        ASSERT_NE(push_response_received_, nullptr);
        EXPECT_EQ(push_response_received_->GetBody(), "Push " + std::to_string(push_id));
    }
}

// Test 10: Push with Large Body
TEST_F(ServerPushRFCTest, LargeBody) {
    std::string large_body(1000, 'X'); // 1KB of 'X'
    
    auto response = std::make_shared<Response>();
    response->SetStatusCode(200);
    response->AddHeader("content-type", "text/plain");
    response->SetBody(large_body);
    
    uint64_t push_id = 99;
    
    ASSERT_TRUE(sender_stream_->SendPushResponse(push_id, response));
    ASSERT_NE(push_response_received_, nullptr);
    EXPECT_EQ(push_response_received_->GetBody().size(), 1000);
    EXPECT_EQ(push_response_received_->GetBody(), large_body);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
