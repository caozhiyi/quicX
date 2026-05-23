// Smoke test for the installed <quicx/...> public API.
//
// We do NOT run this binary; we only verify that:
//   1. headers from <quicx/...> are reachable via find_package(quicx);
//   2. public symbols exist and link against the installed static archives.
//
// Touching one type per public header is enough to keep the test honest.

#include <quicx/common/if_buffer_read.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/if_event_loop.h>
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include <quicx/common/type.h>
#include <quicx/common/version.h>

#include <quicx/quic/if_quic_bidirection_stream.h>
#include <quicx/quic/if_quic_client.h>
#include <quicx/quic/if_quic_connection.h>
#include <quicx/quic/if_quic_recv_stream.h>
#include <quicx/quic/if_quic_send_stream.h>
#include <quicx/quic/if_quic_server.h>
#include <quicx/quic/if_quic_stream.h>
#include <quicx/quic/type.h>

#include <quicx/http3/if_async_handler.h>
#include <quicx/http3/if_client.h>
#include <quicx/http3/if_request.h>
#include <quicx/http3/if_response.h>
#include <quicx/http3/if_server.h>
#include <quicx/http3/type.h>

#include <quicx/upgrade/if_upgrade.h>
#include <quicx/upgrade/type.h>

#include <cstdio>

// Pin the static factory function pointers so the linker has to actually
// resolve them out of libquicx.a / libhttp3.a. Taking the address forces
// the linker to pull the symbol in from the archive without invoking it
// (we do not want runtime-side I/O dependencies in this smoke binary).
static auto* const kHttp3ClientCreate = &quicx::IClient::Create;
static auto* const kHttp3ServerCreate = &quicx::IServer::Create;

int main() {
    (void)kHttp3ClientCreate;
    (void)kHttp3ServerCreate;
    std::printf("quicx install-test: OK (version %s)\n", QUICX_VERSION_STRING);
    return 0;
}
