#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "http3/http/middleware_chain.h"

namespace quicx {
namespace http3 {
namespace {

// The chain itself never dereferences the request/response pointers — it only
// forwards them — so nullptr is safe for unit-testing the pipeline logic.
struct MiddlewareChainTest: public testing::Test {
    std::vector<std::string> calls;
    MiddlewareChain chain;
};

// P1-H8: the middleware's method filter used to be ignored entirely — every
// middleware ran for every request. It must now filter per request method.
TEST_F(MiddlewareChainTest, method_filter_is_enforced) {
    chain.Add(HttpMethod::kGet, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("get-mw");
                  return true;
              });
    chain.Add(HttpMethod::kPost, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("post-mw");
                  return true;
              });

    EXPECT_TRUE(chain.RunBefore(HttpMethod::kGet, nullptr, nullptr));
    EXPECT_EQ(calls, (std::vector<std::string>{"get-mw"}));

    calls.clear();
    EXPECT_TRUE(chain.RunBefore(HttpMethod::kPost, nullptr, nullptr));
    EXPECT_EQ(calls, (std::vector<std::string>{"post-mw"}));
}

// P1-H8: kAny is the bitmask-OR of all implemented methods, so a single AND
// matches every implemented method; kUnknown (not part of kAny) never matches.
TEST_F(MiddlewareChainTest, kany_matches_every_implemented_method) {
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("any-mw");
                  return true;
              });

    EXPECT_TRUE(chain.RunBefore(HttpMethod::kGet, nullptr, nullptr));
    EXPECT_TRUE(chain.RunBefore(HttpMethod::kPost, nullptr, nullptr));
    EXPECT_TRUE(chain.RunBefore(HttpMethod::kPut, nullptr, nullptr));
    EXPECT_EQ(calls, (std::vector<std::string>{"any-mw", "any-mw", "any-mw"}));

    calls.clear();
    EXPECT_TRUE(chain.RunBefore(HttpMethod::kUnknown, nullptr, nullptr));
    EXPECT_TRUE(calls.empty()) << "kUnknown must not be matched even by kAny middlewares";
}

// P1-H8: a before-middleware returning false must stop the remaining
// before-middlewares and report the short-circuit to the caller.
TEST_F(MiddlewareChainTest, before_short_circuit_stops_pipeline) {
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("mw1");
                  return true;
              });
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("mw2");
                  return false;  // short-circuit
              });
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("mw3");
                  return true;
              });

    EXPECT_FALSE(chain.RunBefore(HttpMethod::kGet, nullptr, nullptr));
    EXPECT_EQ(calls, (std::vector<std::string>{"mw1", "mw2"})) << "mw3 must be skipped after the short-circuit";
}

// P1-H8: middlewares run in registration order within each position.
TEST_F(MiddlewareChainTest, registration_order_is_preserved) {
    for (int i = 1; i <= 3; i++) {
        chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore,
                  [this, i](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                      calls.push_back("before-" + std::to_string(i));
                      return true;
                  });
    }
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kAfter,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("after-1");
                  return false;  // ignored at the tail
              });
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kAfter,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("after-2");
                  return true;
              });

    EXPECT_TRUE(chain.RunBefore(HttpMethod::kGet, nullptr, nullptr));
    chain.RunAfter(HttpMethod::kGet, nullptr, nullptr);
    EXPECT_EQ(calls, (std::vector<std::string>{"before-1", "before-2", "before-3", "after-1", "after-2"}));
}

// P1-H8: null handlers must be ignored (mirrors the old null check in
// Server::AddMiddleware).
TEST_F(MiddlewareChainTest, null_handler_is_ignored) {
    middleware_handler null_mw = nullptr;
    chain.Add(HttpMethod::kAny, MiddlewarePosition::kBefore, null_mw);
    EXPECT_TRUE(chain.RunBefore(HttpMethod::kGet, nullptr, nullptr));
    EXPECT_TRUE(calls.empty());
}

// P1-H8: after-middlewares are filtered by method as well.
TEST_F(MiddlewareChainTest, after_pipeline_is_method_filtered) {
    chain.Add(HttpMethod::kGet, MiddlewarePosition::kAfter,
              [this](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) {
                  calls.push_back("get-after");
                  return true;
              });

    chain.RunAfter(HttpMethod::kPost, nullptr, nullptr);
    EXPECT_TRUE(calls.empty());

    chain.RunAfter(HttpMethod::kGet, nullptr, nullptr);
    EXPECT_EQ(calls, (std::vector<std::string>{"get-after"}));
}

// P1-H8: bitmask helper — one implemented method against another never
// matches; kAny matches every implemented method.
TEST_F(MiddlewareChainTest, method_matches_bitmask_semantics) {
    EXPECT_TRUE(MiddlewareChain::MethodMatches(HttpMethod::kGet, HttpMethod::kGet));
    EXPECT_FALSE(MiddlewareChain::MethodMatches(HttpMethod::kGet, HttpMethod::kPost));
    EXPECT_TRUE(MiddlewareChain::MethodMatches(HttpMethod::kAny, HttpMethod::kGet));
    EXPECT_TRUE(MiddlewareChain::MethodMatches(HttpMethod::kAny, HttpMethod::kPost));
    EXPECT_FALSE(MiddlewareChain::MethodMatches(HttpMethod::kAny, HttpMethod::kUnknown));
    EXPECT_FALSE(MiddlewareChain::MethodMatches(HttpMethod::kGet, HttpMethod::kUnknown));
}

}  // namespace
}  // namespace http3
}  // namespace quicx
