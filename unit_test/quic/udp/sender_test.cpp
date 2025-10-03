#include <thread>
#include <gtest/gtest.h>
#include "quic/udp/udp_sender.h"
#include "common/buffer/buffer.h"
#include "quic/udp/udp_receiver.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace quic {
namespace {

int g_send_times = 5;
int g_recv_times = 0;

class RecvHandler: public IPacketReceiver {
public:
    void OnPacket(std::shared_ptr<NetPacket>& pkt) override {
        g_recv_times++;
    }
};

TEST(UdpSenderTest, Send) {
    auto recv_handler = std::make_shared<RecvHandler>();
    std::vector<std::thread> threads;
    std::shared_ptr<common::IEventLoop> event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());

    auto receiver = std::make_shared<UdpReceiver>(event_loop);
    receiver->AddReceiver("127.0.0.1", 12345, recv_handler);

    threads.emplace_back([event_loop]() {
        char recv_buf[200] = {0};
        std::shared_ptr<common::Buffer> recv_buffer = std::make_shared<common::Buffer>((uint8_t*)recv_buf, sizeof(recv_buf));

        std::shared_ptr<NetPacket> recv_pkt = std::make_shared<NetPacket>();
        recv_pkt->SetData(recv_buffer);

        while (g_recv_times < g_send_times) {
            event_loop->Wait();
        }
    });
    
    auto sockfd_ret = common::UdpSocket();
    ASSERT_EQ(sockfd_ret.errno_, 0);
    UdpSender sender(sockfd_ret.return_value_);
    
    char send_buf[20] = {0};
    std::shared_ptr<common::Buffer> send_buffer = std::make_shared<common::Buffer>((uint8_t*)send_buf, sizeof(send_buf));
    send_buffer->Write((uint8_t*)("hello world"), sizeof("hello world"));

    std::shared_ptr<NetPacket> send_pkt = std::make_shared<NetPacket>();
    send_pkt->SetData(send_buffer);

    common::Address addr("127.0.0.1", 12345);
    send_pkt->SetAddress(addr);

    
    for (int i = 0; i < g_send_times; ++i) {
        // wait receiver thread ready
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sender.Send(send_pkt);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_EQ(g_recv_times, g_send_times);
}

}
}
}