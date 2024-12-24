#include <gtest/gtest.h>
#include "http3/http/router/router.h"

namespace quicx {
namespace http3 {
namespace {

TEST(router, add_router) {
    Router router;
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/:paramA/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/:paramA/user/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/home/*", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_POST, "/", nullptr));
    EXPECT_FALSE(router.AddRoute(HttpMothed::HM_POST, "", nullptr));
    EXPECT_FALSE(router.AddRoute(HttpMothed::HM_POST, "/test/home/*/other", nullptr));
}

TEST(router, match) {
    Router router;
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/:paramA/:paramB", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/:paramC/user/:paramD", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/test/home/*", nullptr));
    EXPECT_TRUE(router.AddRoute(HttpMothed::HM_GET, "/", nullptr));

    EXPECT_TRUE(router.Match(HttpMothed::HM_GET, "/test/123/456").is_match);
    EXPECT_TRUE(router.Match(HttpMothed::HM_GET, "/test/123/user/456").is_match);
    EXPECT_TRUE(router.Match(HttpMothed::HM_GET, "/test/home/123").is_match);
    EXPECT_TRUE(router.Match(HttpMothed::HM_GET, "/").is_match);
    
    EXPECT_FALSE(router.Match(HttpMothed::HM_GET, "/test/other").is_match);
    EXPECT_FALSE(router.Match(HttpMothed::HM_GET, "").is_match);
    EXPECT_FALSE(router.Match(HttpMothed::HM_POST, "/test/123/456").is_match);
}

}
}
}