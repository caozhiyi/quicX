#include <gtest/gtest.h>
#include "http3/router/router.h"

namespace quicx {
namespace http3 {
namespace {

TEST(router, add_router) {
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/:paramB", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/user/:paramB", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/home/*", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kPost, "/", config));
    EXPECT_FALSE(router.AddRoute(HttpMethod::kPost, "", config));
    EXPECT_FALSE(router.AddRoute(HttpMethod::kPost, "/test/home/*/other", config));
}

TEST(router, match) {
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/:paramB", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramC/user/:paramD", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/home/*", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/", config));

    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/test/123/456").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/test/123/user/456").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/test/home/123").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/").is_match);
    
    EXPECT_FALSE(router.Match(HttpMethod::kGet, "/test/other").is_match);
    EXPECT_FALSE(router.Match(HttpMethod::kGet, "").is_match);
    EXPECT_FALSE(router.Match(HttpMethod::kPost, "/test/123/456").is_match);
}

TEST(router, match_single_path_param) {
    // Reproduce AdvancedFeaturesTest.SinglePathParam scenario:
    //   AddRoute("/users/:id"), Match("/users/42")
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id", config));

    auto r = router.Match(HttpMethod::kGet, "/users/42");
    EXPECT_TRUE(r.is_match);
    auto it = r.params.find("id");
    EXPECT_NE(it, r.params.end());
    if (it != r.params.end()) {
        EXPECT_EQ(it->second, "42");
    }

    auto r2 = router.Match(HttpMethod::kGet, "/users/john");
    EXPECT_TRUE(r2.is_match);
    auto it2 = r2.params.find("id");
    EXPECT_NE(it2, r2.params.end());
    if (it2 != r2.params.end()) {
        EXPECT_EQ(it2->second, "john");
    }
}

TEST(router, match_path_param_with_sibling_nested) {
    // Reproduce AdvancedFeaturesTest scenario where BOTH /users/:id and
    // /users/:user_id/posts/:post_id are registered together. Match a single
    // segment path /users/42 must still match /users/:id and extract
    // params["id"] = "42" (NOT params["user_id"] = "42").
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:user_id/posts/:post_id", config));

    auto r = router.Match(HttpMethod::kGet, "/users/42");
    EXPECT_TRUE(r.is_match);
    auto it = r.params.find("id");
    EXPECT_NE(it, r.params.end());
    if (it != r.params.end()) {
        EXPECT_EQ(it->second, "42");
    }
    // Make sure /users/:user_id sibling did NOT pollute the result.
    EXPECT_EQ(r.params.find("user_id"), r.params.end());
    EXPECT_EQ(r.params.find("post_id"), r.params.end());

    auto r2 = router.Match(HttpMethod::kGet, "/users/john");
    EXPECT_TRUE(r2.is_match);
    auto it2 = r2.params.find("id");
    EXPECT_NE(it2, r2.params.end());
    if (it2 != r2.params.end()) {
        EXPECT_EQ(it2->second, "john");
    }
    EXPECT_EQ(r2.params.find("user_id"), r2.params.end());

    // Nested path still matches.
    auto r3 = router.Match(HttpMethod::kGet, "/users/5/posts/100");
    EXPECT_TRUE(r3.is_match);
    auto it_uid = r3.params.find("user_id");
    auto it_pid = r3.params.find("post_id");
    EXPECT_NE(it_uid, r3.params.end());
    EXPECT_NE(it_pid, r3.params.end());
    if (it_uid != r3.params.end()) EXPECT_EQ(it_uid->second, "5");
    if (it_pid != r3.params.end()) EXPECT_EQ(it_pid->second, "100");
}

TEST(router, match_advanced_features_full_set) {
    // Reproduce ALL routes registered by AdvancedFeaturesTest::RegisterHandlers
    // and verify that /users/42 matches /users/:id.  This is the exact
    // failing scenario in the integration test.
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:user_id/posts/:post_id", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/echo-headers", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/search", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/stream-response", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/multi-headers", config));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/middleware-test", config));

    auto r = router.Match(HttpMethod::kGet, "/users/42");
    EXPECT_TRUE(r.is_match);
    auto it = r.params.find("id");
    EXPECT_NE(it, r.params.end());
    if (it != r.params.end()) {
        EXPECT_EQ(it->second, "42");
    }

    auto r2 = router.Match(HttpMethod::kGet, "/users/john");
    EXPECT_TRUE(r2.is_match);

    auto r3 = router.Match(HttpMethod::kGet, "/users/5/posts/100");
    EXPECT_TRUE(r3.is_match);
}

}
}
}