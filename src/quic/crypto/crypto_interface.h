#ifndef QUIC_CRYPTO_CRYPTO_INTERFACE
#define QUIC_CRYPTO_CRYPTO_INTERFACE

namespace quicx {


// 初始加密随机字符串
const char* initial_salt = "0xc3eef712c72ebb5a11a7d2432bb46365bef9f502";
/*
1. 发送和接收握手信息
2. 从恢复的会话中处理传输和程序状态, 并确定接收早期数据是否有效
3. 重新键入(发送/接收)
4. 握手状态更新, 配置TLS可能需要的其他功能
*/

class Crypto {
public:
    Crypto() {}
    ~Crypto() {}

    void GetHandshake() {}
    void RecvHandshake() {}

    void InstallHandshakekeys() {}
};

}

#endif