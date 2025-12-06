#include <thread>
#include <atomic>
#include <gtest/gtest.h>
#include <cstring>
#include "quic/udp/udp_sender.h"
#include "quic/udp/udp_receiver.h"
#include "common/network/io_handle.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

int g_send_times = 5;
int g_recv_times = 0;

class RecvHandler: public IPacketReceiver {
public:
    void OnPacket(std::shared_ptr<NetPacket>& pkt) override { g_recv_times++; }
};

TEST(UdpSenderTest, Send) {
    auto event_loop = common::MakeEventLoop();
    
    auto recv_handler = std::make_shared<RecvHandler>();
    std::vector<std::thread> threads;
    auto receiver = std::make_shared<UdpReceiver>(event_loop);
    std::atomic<bool> receiver_ready{false};

    threads.emplace_back([receiver, recv_handler, &receiver_ready, event_loop]() {
        ASSERT_TRUE(event_loop->Init());
        // AddReceiver must be called in the same thread that will call Wait()
        // because EventLoop is thread_local
        ASSERT_TRUE(receiver->AddReceiver("127.0.0.1", 1121, recv_handler));
        receiver_ready = true;
        
        char recv_buf[200] = {0};
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(200);
        std::shared_ptr<common::SingleBlockBuffer> recv_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
        recv_buffer->Write((uint8_t*)recv_buf, 200);

        std::shared_ptr<NetPacket> recv_pkt = std::make_shared<NetPacket>();
        recv_pkt->SetData(recv_buffer);

        while (g_recv_times < g_send_times) {
            event_loop->Wait();
        }
    });

    // Wait for receiver thread to be ready
    while (!receiver_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto sockfd_ret = common::UdpSocket();
    ASSERT_EQ(sockfd_ret.errno_, 0);
    UdpSender sender(sockfd_ret.return_value_);

    char send_buf[20] = {0};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(sizeof(send_buf));
    ASSERT_TRUE(chunk->Valid());
    std::memcpy(chunk->GetData(), send_buf, sizeof(send_buf));
    std::shared_ptr<common::SingleBlockBuffer> send_buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    send_buffer->Write((uint8_t*)("hello world"), sizeof("hello world"));

    std::shared_ptr<NetPacket> send_pkt = std::make_shared<NetPacket>();
    send_pkt->SetData(send_buffer);

    common::Address addr("127.0.0.1", 1121);
    send_pkt->SetAddress(addr);

    for (int i = 0; i < g_send_times; ++i) {
        sender.Send(send_pkt);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(g_recv_times, g_send_times);
}

}  // namespace
}  // namespace quic
}  // namespace quicx