#ifndef UTEST_CONNECTION_TEST_UTIL
#define UTEST_CONNECTION_TEST_UTIL

#include <memory>
#include <vector>
#include "quic/connection/if_connection.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/packet_decode.h"
#include "common/buffer/if_buffer.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "mock_sender.h"

namespace quicx {
namespace quic {

/**
 * @brief Test utility functions for connection testing
 *
 * Provides helper functions to replace the deprecated GenerateSendData() method.
 */

/**
 * @brief Create a MockSender and attach it to a connection
 *
 * @param conn Connection to attach sender to
 * @return Shared pointer to the created MockSender
 */
inline std::shared_ptr<MockSender> AttachMockSender(std::shared_ptr<IConnection> conn) {
    auto mock_sender = std::make_shared<MockSender>();
    conn->SetSender(mock_sender);
    return mock_sender;
}

/**
 * @brief Generate send data using TrySend() and return buffer
 *
 * This is a compatibility helper that mimics the old GenerateSendData() behavior
 * for tests, but uses the new TrySend() + MockSender approach internally.
 *
 * @param conn Connection to send from
 * @param buffer Output buffer (for compatibility - gets filled with sent data)
 * @param send_operation Output send operation state
 * @param mock_sender MockSender to capture data (if null, creates temporary one)
 * @return true if data was generated
 */
inline bool GenerateSendDataCompat(std::shared_ptr<IConnection> conn,
                                   std::shared_ptr<common::IBuffer> buffer,
                                   SendOperation& send_operation,
                                   std::shared_ptr<MockSender> mock_sender = nullptr) {
    if (!conn || !buffer) {
        return false;
    }

    // Create temporary mock sender if not provided
    std::shared_ptr<MockSender> sender = mock_sender;
    bool temp_sender = false;
    if (!sender) {
        sender = std::make_shared<MockSender>();
        conn->SetSender(sender);
        temp_sender = true;
    }

    // Clear previous data
    sender->Clear();

    // Trigger send
    if (!conn->TrySend()) {
        send_operation = SendOperation::kAllSendDone;
        return false;
    }

    // Copy sent data to output buffer
    auto sent_buffer = sender->GetLastSentBuffer();
    if (!sent_buffer || sent_buffer->GetDataLength() == 0) {
        send_operation = SendOperation::kAllSendDone;
        return false;
    }

    // Copy data to output buffer
    auto span = sent_buffer->GetReadableSpan();
    if (span.Valid() && span.GetLength() > 0) {
        buffer->Write(span.GetStart(), span.GetLength());
    }
    send_operation = SendOperation::kSendAgainImmediately;

    return true;
}

/**
 * @brief Helper to process packets between two connections
 *
 * Replaces the ConnectionProcess() function that used GenerateSendData().
 *
 * @param send_conn Connection to send from
 * @param recv_conn Connection to receive at
 * @param sender_mock MockSender attached to send_conn
 * @return true if packets were successfully sent and received
 */
inline bool ConnectionProcess(std::shared_ptr<IConnection> send_conn,
                              std::shared_ptr<IConnection> recv_conn,
                              std::shared_ptr<MockSender> sender_mock) {
    if (!send_conn || !recv_conn || !sender_mock) {
        return false;
    }

    // Clear previous packets
    sender_mock->Clear();

    // Trigger send
    if (!send_conn->TrySend()) {
        return false;
    }

    // Get sent buffer
    auto buffer = sender_mock->GetLastSentBuffer();
    if (!buffer || buffer->GetDataLength() == 0) {
        return false;
    }

    // Decode packets
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets)) {
        return false;
    }

    // Deliver to receiver
    recv_conn->OnPackets(0, packets);
    return true;
}

}  // namespace quic
}  // namespace quicx

#endif  // UTEST_CONNECTION_TEST_UTIL
