#include <gtest/gtest.h>
#include "http3/router/router.h"

namespace quicx {
namespace http3 {
namespace {

TEST(router, add_router) {
    Router router;
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/user/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/home/*", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kPost, "/", nullptr));
    EXPECT_FALSE(router.AddRoute(HttpMethod::kPost, "", nullptr));
    EXPECT_FALSE(router.AddRoute(HttpMethod::kPost, "/test/home/*/other", nullptr));
}

TEST(router, match) {
    Router router;
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramA/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/:paramC/user/:paramD", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/test/home/*", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/", nullptr));

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