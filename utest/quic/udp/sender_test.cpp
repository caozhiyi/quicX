#include <thread>
#include <gtest/gtest.h>
#include "quic/udp/udp_sender.h"
#include "common/buffer/buffer.h"
#include "quic/udp/udp_receiver.h"

namespace quicx {
namespace quic {
namespace {

int g_send_times = 5;

TEST(UdpSenderTest, Send) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 1; ++i) {
        threads.emplace_back([]() {
            char recv_buf[200] = {0};
            std::shared_ptr<common::Buffer> recv_buffer = std::make_shared<common::Buffer>((uint8_t*)recv_buf, sizeof(recv_buf));

            std::shared_ptr<NetPacket> recv_pkt = std::make_shared<NetPacket>();
            recv_pkt->SetData(recv_buffer);

            UdpReceiver receiver;
            receiver.AddReceiver("127.0.0.1", 12345);

            for (int i = 0; i < g_send_times; ++i) {
                receiver.TryRecv(recv_pkt, 10000);
            }
        });
    }
    
    UdpSender sender;
    
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
}

}
}
}