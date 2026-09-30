#ifndef TOOL_QC_FEATURES_PUSH_DISPLAY_H
#define TOOL_QC_FEATURES_PUSH_DISPLAY_H

#include "obs/tracer.h"

namespace quicx {
class IClient;
}

// Server-push display (--push): promises are announced on stderr, pushed
// responses are summarized (status + body size) — pushed payloads never mix
// into the main body channel.
void InstallPushHandlers(quicx::IClient& client, const Tracer& tracer);

#endif  // TOOL_QC_FEATURES_PUSH_DISPLAY_H
