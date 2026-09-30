// quicx_curl: HTTP/3 command-line tool (curl-like + QUIC feature showcase).
// Assembly root only: parse options, wire session + orchestrator, run.

#include <csignal>
#include <cstdio>
#include <memory>

#include <quicx/http3/if_client.h>  // complete type for unique_ptr<IClient> dtor

#include "app/orchestrator.h"
#include "cli/option_table.h"
#include "obs/tracer.h"
#include "session/session.h"

namespace {

ClientSession* g_active_session = nullptr;

extern "C" void HandleInterrupt(int sig) {
    // Best-effort graceful close (CONNECTION_CLOSE + qlog/keylog flush),
    // then exit with the conventional 128+SIGINT code like curl does.
    if (g_active_session) g_active_session->Close();
    _exit(128 + sig);
}

}  // namespace

int main(int argc, char* argv[]) {
    OptionTable table;
    Options opts;
    std::string error;
    if (!table.Parse(argc, argv, opts, error)) {
        std::fprintf(stderr, "quicx-curl: %s\n", error.c_str());
        table.PrintHelp(argv[0], stderr);
        return 3;  // URL malformat (curl: bad option -> exit 2; close enough family)
    }
    if (opts.show_help) {
        table.PrintHelp(argv[0], stdout);
        return 0;
    }
    if (!opts.IsValid()) {
        std::fprintf(stderr, "quicx-curl: no URL given\n\n");
        table.PrintHelp(argv[0], stderr);
        return 2;  // curl: failed to initialize / usage error
    }

    Tracer tracer(opts.out.verbose, opts.out.show_error || !opts.out.silent);
    tracer.Info("quicx-curl 1.0 (HTTP/3, quicx)");

    auto session = std::make_unique<ClientSession>();
    if (!session->Init(opts.tp, opts.req.connect_timeout_s, opts.out.verbose)) return 7;  // connection failure

    g_active_session = session.get();
    std::signal(SIGINT, HandleInterrupt);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);  // broken pipe on stdout must not kill us silently
#endif

    Orchestrator orch(opts);
    int code = orch.Run(*session);

    g_active_session = nullptr;
    session->Close();
    return code;
}
