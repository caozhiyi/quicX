#include <gtest/gtest.h>
#include "http3/http/router/router_node_root.h"

namespace quicx {
namespace http3 {
namespace {

TEST(router_node, add_router) {
    RouterNodeRoot root;
    EXPECT_TRUE(root.AddRoute("/test/:paramA/:paramB", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/test/:paramA/user/:paramB", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/test/home/*", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/", 0, nullptr));
    EXPECT_FALSE(root.AddRoute("", 0, nullptr));
    EXPECT_FALSE(root.AddRoute("/test/home/*/other", 0, nullptr));
}

TEST(router_node, match) {
    RouterNodeRoot root;
    EXPECT_TRUE(root.AddRoute("/test/:paramA/:paramB", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/test/:paramC/user/:paramD", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/test/home/*", 0, nullptr));
    EXPECT_TRUE(root.AddRoute("/", 0, nullptr));

    MatchResult result;
    EXPECT_TRUE(root.Match("/test/123/456", 0, "", result));

    MatchResult result1;
    EXPECT_TRUE(root.Match("/test/123/user/456", 0, "", result1));

    MatchResult result2;
    EXPECT_TRUE(root.Match("/test/home/123", 0, "", result2));

    MatchResult result3;
    EXPECT_FALSE(root.Match("/test/other", 0, "", result3));

    EXPECT_TRUE(root.Match("/", 0, "", result3));

    EXPECT_FALSE(root.Match("", 0, "", result3));
}

}
}
}