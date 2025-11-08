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

}
}
}