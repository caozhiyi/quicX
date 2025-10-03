#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

#include "upgrade/network/tcp_socket.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {
namespace {

class ConnectionContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        socket_ = std::make_shared<TcpSocket>();
    }
    
    void TearDown() override {
        socket_.reset();
    }
    
    std::shared_ptr<ITcpSocket> socket_;
};

// Test connection context creation
TEST_F(ConnectionContextTest, Creation) {
    ConnectionContext context(socket_);
    
    EXPECT_EQ(context.socket, socket_);
    EXPECT_EQ(context.state, ConnectionState::INITIAL);
    EXPECT_EQ(context.detected_protocol, Protocol::UNKNOWN);
    EXPECT_EQ(context.target_protocol, Protocol::UNKNOWN);
    EXPECT_TRUE(context.initial_data.empty());
    EXPECT_TRUE(context.alpn_protocols.empty());
    EXPECT_EQ(context.response_sent, 0);
    EXPECT_EQ(context.negotiation_timer_id, 0);
}

// Test connection context state transitions
TEST_F(ConnectionContextTest, StateTransitions) {
    ConnectionContext context(socket_);
    
    // Initial state
    EXPECT_EQ(context.state, ConnectionState::INITIAL);
    
    // Transition to detecting
    context.state = ConnectionState::DETECTING;
    EXPECT_EQ(context.state, ConnectionState::DETECTING);
    
    // Transition to negotiating
    context.state = ConnectionState::NEGOTIATING;
    EXPECT_EQ(context.state, ConnectionState::NEGOTIATING);
    
    // Transition to upgraded
    context.state = ConnectionState::UPGRADED;
    EXPECT_EQ(context.state, ConnectionState::UPGRADED);
    
    // Transition to failed
    context.state = ConnectionState::FAILED;
    EXPECT_EQ(context.state, ConnectionState::FAILED);
}

// Test protocol detection
TEST_F(ConnectionContextTest, ProtocolDetection) {
    ConnectionContext context(socket_);
    
    // Test protocol detection
    context.detected_protocol = Protocol::HTTP1_1;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP1_1);
    
    context.detected_protocol = Protocol::HTTP2;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP2);
    
    context.detected_protocol = Protocol::HTTP3;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP3);
}

// Test target protocol setting
TEST_F(ConnectionContextTest, TargetProtocol) {
    ConnectionContext context(socket_);
    
    // Set target protocol
    context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(context.target_protocol, Protocol::HTTP3);
    
    context.target_protocol = Protocol::HTTP2;
    EXPECT_EQ(context.target_protocol, Protocol::HTTP2);
}

// Test initial data storage
TEST_F(ConnectionContextTest, InitialData) {
    ConnectionContext context(socket_);
    
    // Add initial data
    std::vector<uint8_t> data = {0x48, 0x54, 0x54, 0x50}; // "HTTP"
    context.initial_data = data;
    
    EXPECT_EQ(context.initial_data.size(), 4);
    EXPECT_EQ(context.initial_data[0], 0x48);
    EXPECT_EQ(context.initial_data[1], 0x54);
    EXPECT_EQ(context.initial_data[2], 0x54);
    EXPECT_EQ(context.initial_data[3], 0x50);
}

// Test ALPN protocols
TEST_F(ConnectionContextTest, AlpnProtocols) {
    ConnectionContext context(socket_);
    
    // Add ALPN protocols
    context.alpn_protocols.push_back("h3");
    context.alpn_protocols.push_back("h2");
    context.alpn_protocols.push_back("http/1.1");
    
    EXPECT_EQ(context.alpn_protocols.size(), 3);
    EXPECT_EQ(context.alpn_protocols[0], "h3");
    EXPECT_EQ(context.alpn_protocols[1], "h2");
    EXPECT_EQ(context.alpn_protocols[2], "http/1.1");
}

// Test created time
TEST_F(ConnectionContextTest, CreatedTime) {
    auto before = std::chrono::steady_clock::now();
    ConnectionContext context(socket_);
    auto after = std::chrono::steady_clock::now();
    
    EXPECT_GE(context.created_time, before);
    EXPECT_LE(context.created_time, after);
}

// Test pending response data
TEST_F(ConnectionContextTest, PendingResponse) {
    ConnectionContext context(socket_);
    
    // Set pending response
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    
    EXPECT_EQ(context.pending_response.size(), response.length());
    EXPECT_EQ(context.response_sent, 0);
    
    // Simulate partial send
    context.response_sent = 20;
    EXPECT_EQ(context.response_sent, 20);
    EXPECT_LT(context.response_sent, context.pending_response.size());
}

// Test negotiation timer ID
TEST_F(ConnectionContextTest, NegotiationTimer) {
    ConnectionContext context(socket_);
    
    // Set timer ID
    context.negotiation_timer_id = 12345;
    EXPECT_EQ(context.negotiation_timer_id, 12345);
    
    // Clear timer ID
    context.negotiation_timer_id = 0;
    EXPECT_EQ(context.negotiation_timer_id, 0);
}

// Test complete response sending
TEST_F(ConnectionContextTest, CompleteResponseSending) {
    ConnectionContext context(socket_);
    
    // Set pending response
    std::string response = "HTTP/1.1 200 OK\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    
    // Simulate complete send
    context.response_sent = context.pending_response.size();
    EXPECT_EQ(context.response_sent, context.pending_response.size());
    
    // Response should be considered complete
    EXPECT_TRUE(context.response_sent >= context.pending_response.size());
}

// Test multiple connection contexts
TEST_F(ConnectionContextTest, MultipleContexts) {
    auto socket1 = std::make_shared<TcpSocket>();
    auto socket2 = std::make_shared<TcpSocket>();
    
    ConnectionContext context1(socket1);
    ConnectionContext context2(socket2);
    
    // Different sockets
    EXPECT_NE(context1.socket, context2.socket);
    EXPECT_EQ(context1.socket, socket1);
    EXPECT_EQ(context2.socket, socket2);
    
    // Different creation times
    EXPECT_NE(context1.created_time, context2.created_time);
    
    // Different file descriptors
    EXPECT_NE(context1.socket->GetFd(), context2.socket->GetFd());
}

// Test context with different protocols
TEST_F(ConnectionContextTest, DifferentProtocols) {
    ConnectionContext context(socket_);
    
    // HTTP/1.1 detection
    context.detected_protocol = Protocol::HTTP1_1;
    context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP1_1);
    EXPECT_EQ(context.target_protocol, Protocol::HTTP3);
    
    // HTTP/2 detection
    context.detected_protocol = Protocol::HTTP2;
    context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP2);
    EXPECT_EQ(context.target_protocol, Protocol::HTTP3);
    
    // Direct HTTP/3
    context.detected_protocol = Protocol::HTTP3;
    context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP3);
    EXPECT_EQ(context.target_protocol, Protocol::HTTP3);
}

// Test context cleanup
TEST_F(ConnectionContextTest, ContextCleanup) {
    ConnectionContext context(socket_);
    
    // Add some data
    context.initial_data = {0x01, 0x02, 0x03, 0x04};
    context.alpn_protocols = {"h3", "h2"};
    context.pending_response = {0x05, 0x06, 0x07, 0x08};
    context.response_sent = 2;
    context.negotiation_timer_id = 999;
    
    // Verify data is present
    EXPECT_FALSE(context.initial_data.empty());
    EXPECT_FALSE(context.alpn_protocols.empty());
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_GT(context.response_sent, 0);
    EXPECT_GT(context.negotiation_timer_id, 0);
    
    // Context will be cleaned up when it goes out of scope
    // This test verifies no memory leaks occur
}

}
} // namespace upgrade
} // namespace quicx 