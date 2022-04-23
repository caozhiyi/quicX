#include "quic/controller/controller.h"

int main() {
    quicx::Controller controller;
    controller.Listen("0.0.0.0", 8090);
    return 0;
}