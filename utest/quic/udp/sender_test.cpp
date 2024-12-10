#include <thread>
#include <gtest/gtest.h>
#include "quic/udp/udp_sender.h"
#include "common/buffer/buffer.h"
#include "quic/udp/udp_receiver.h"
#include "quic/quicx/if_net_packet.h"

namespace quicx {
namespace quic {
namespace {

TEST(UdpSenderTest, Send) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([]() {
            char recv_buf[20] = {0};
            std::shared_ptr<common::Buffer> recv_buffer = std::make_shared<common::Buffer>((uint8_t*)recv_buf, sizeof(recv_buf));

            std::shared_ptr<INetPacket> recv_pkt = std::make_shared<INetPacket>();
            recv_pkt->SetData(recv_buffer);

            UdpReceiver receiver("127.0.0.1", 12345);
            while (3) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if (receiver.TryRecv(recv_pkt) == IReceiver::RR_SUCCESS){
                    EXPECT_EQ(0, memcmp(recv_buf, "hello world", sizeof("hello world")));
                    break;
                }
            }
        });
    }
    
    UdpSender sender;
    
    char send_buf[20] = {0};
    std::shared_ptr<common::Buffer> send_buffer = std::make_shared<common::Buffer>((uint8_t*)send_buf, sizeof(send_buf));
    send_buffer->Write((uint8_t*)("hello world"), sizeof("hello world"));

    std::shared_ptr<INetPacket> send_pkt = std::make_shared<INetPacket>();
    send_pkt->SetData(send_buffer);

    common::Address addr(common::AddressType::AT_IPV4, "127.0.0.1", 12345);
    send_pkt->SetAddress(addr);

    
    for (int i = 0; i < 5; ++i) {
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