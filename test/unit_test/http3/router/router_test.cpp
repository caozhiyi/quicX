#include <gtest/gtest.h>

#include "http3/router/router.h"

namespace quicx {
namespace http3 {
namespace {

TEST(RouterTest, add_router) {
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

TEST(RouterTest, match) {
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

TEST(RouterTest, match_single_path_param) {
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

TEST(RouterTest, match_path_param_with_sibling_nested) {
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
    if (it_uid != r3.params.end()) {
        EXPECT_EQ(it_uid->second, "5");
    }
    if (it_pid != r3.params.end()) {
        EXPECT_EQ(it_pid->second, "100");
    }
}

TEST(RouterTest, match_advanced_features_full_set) {
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

// #7 (code review round 2): two same-level dynamic-parameter names whose
// routes BOTH fully match a one-segment path ("/users/:id" vs "/users/:name"
// for "/users/42") previously won in unordered_map hash order — the bound
// parameter key was unspecified. dynamic_param_map_ is now a std::map, so
// the lexicographically smaller section (":id") must win deterministically.
TEST(RouterTest, same_level_dynamic_params_match_deterministically) {
    Router router;
    http_handler null_handler = nullptr;
    RouteConfig config(null_handler);
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:name", config));
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id", config));

    auto r = router.Match(HttpMethod::kGet, "/users/42");
    ASSERT_TRUE(r.is_match);
    EXPECT_NE(r.params.find("id"), r.params.end())
        << "\":id\" (lexicographically smaller) must win the tie deterministically";
    EXPECT_EQ(r.params.find("name"), r.params.end());

    // Different param names at the same level for structurally-different
    // routes keep working (backtrack semantics, not iteration order):
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:user_id/posts/:post_id", config));
    auto r2 = router.Match(HttpMethod::kGet, "/users/5/posts/100");
    ASSERT_TRUE(r2.is_match);
    EXPECT_NE(r2.params.find("user_id"), r2.params.end());
    EXPECT_NE(r2.params.find("post_id"), r2.params.end());
    auto r3 = router.Match(HttpMethod::kGet, "/users/7");
    ASSERT_TRUE(r3.is_match);
    EXPECT_NE(r3.params.find("id"), r3.params.end());
}

// P1-H8 (round 3): a node must be able to carry BOTH a handler and children.
// Registering "/api/v1/users" first created "/api/v1" as a middle node; the
// later registration of "/api/v1" used to have its handler silently dropped
// because leaf-ness was decided at node creation time.
TEST(RouterTest, shorter_prefix_upgrades_existing_middle_node) {
    Router router;
    http_handler null_handler = nullptr;
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/api/v1/users", RouteConfig(null_handler)));
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/api/v1", RouteConfig(null_handler)));

    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/api/v1").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/api/v1/users").is_match);
}

// P1-H8 (round 3): registering the exact same method+path twice used to
// silently keep the first handler. It must now be rejected explicitly.
TEST(RouterTest, duplicate_route_registration_is_rejected) {
    Router router;
    http_handler null_handler = nullptr;
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/dup", RouteConfig(null_handler)));
    EXPECT_FALSE(router.AddRoute(HttpMethod::kGet, "/dup", RouteConfig(null_handler)));

    // Same path under a different method is not a duplicate.
    EXPECT_TRUE(router.AddRoute(HttpMethod::kPost, "/dup", RouteConfig(null_handler)));
    // Deeper route under an existing leaf keeps working.
    EXPECT_TRUE(router.AddRoute(HttpMethod::kGet, "/dup/child", RouteConfig(null_handler)));
    // Duplicate check also holds for nested and param/wildcard leaves.
    EXPECT_FALSE(router.AddRoute(HttpMethod::kGet, "/dup/child", RouteConfig(null_handler)));

    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/dup").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kPost, "/dup").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/dup/child").is_match);
}

// P1-H8 (round 3): a route registered under kAny used to be unreachable —
// Match() only looked up the tree of the request method.
TEST(RouterTest, kany_tree_is_served_as_method_fallback) {
    Router router;
    http_handler null_handler = nullptr;
    EXPECT_TRUE(router.AddRoute(HttpMethod::kAny, "/fallback", RouteConfig(null_handler)));

    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/fallback").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kPost, "/fallback").is_match);
    EXPECT_TRUE(router.Match(HttpMethod::kDelete, "/fallback").is_match);
}

// P1-H8 (round 3): the method-specific tree takes precedence over the kAny
// tree; kAny only serves what the method tree cannot match.
TEST(RouterTest, method_tree_takes_precedence_over_kany) {
    Router router;
    static std::string hit;
    hit.clear();
    http_handler get_handler = [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) { hit = "get-tree"; };
    http_handler any_handler = [](std::shared_ptr<IRequest>, std::shared_ptr<IResponse>) { hit = "kany-tree"; };

    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/prio", RouteConfig(get_handler)));
    ASSERT_TRUE(router.AddRoute(HttpMethod::kAny, "/prio", RouteConfig(any_handler)));
    ASSERT_TRUE(router.AddRoute(HttpMethod::kAny, "/only-any", RouteConfig(any_handler)));

    auto r1 = router.Match(HttpMethod::kGet, "/prio");
    ASSERT_TRUE(r1.is_match);
    ASSERT_TRUE(r1.config.GetCompleteHandler());
    r1.config.GetCompleteHandler()(nullptr, nullptr);
    EXPECT_EQ(hit, "get-tree");  // exact method tree wins for GET

    auto r2 = router.Match(HttpMethod::kPost, "/prio");
    ASSERT_TRUE(r2.is_match);
    ASSERT_TRUE(r2.config.GetCompleteHandler());
    r2.config.GetCompleteHandler()(nullptr, nullptr);
    EXPECT_EQ(hit, "kany-tree");  // POST tree absent -> kAny serves

    auto r3 = router.Match(HttpMethod::kGet, "/only-any");
    ASSERT_TRUE(r3.is_match);  // GET tree has no /only-any -> kAny fallback

    EXPECT_FALSE(router.Match(HttpMethod::kGet, "/nowhere").is_match);  // nothing anywhere
}

// P1-H8 (round 3): a failed method-tree match may have left parameter
// bindings from partially matched dynamic subtrees; the kAny fallback must
// not inherit them.
TEST(RouterTest, kany_fallback_does_not_inherit_stale_params) {
    Router router;
    http_handler null_handler = nullptr;
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id/settings", RouteConfig(null_handler)));
    ASSERT_TRUE(router.AddRoute(HttpMethod::kAny, "/users/*", RouteConfig(null_handler)));

    // "/users/42" walks the GET tree: ":id" matches but has no handler at
    // depth 1 ("settings" is at depth 2) -> subtree fails, wildcard in the
    // GET tree is absent -> falls back to the kAny tree.
    auto r = router.Match(HttpMethod::kGet, "/users/42");
    ASSERT_TRUE(r.is_match);
    EXPECT_TRUE(r.params.empty()) << "stale ':id' binding leaked into the kAny fallback result";
}

// P1-H8 (round 3): backtracking out of a failed dynamic subtree must clean
// the bindings that subtree wrote (e.g. a wildcard sibling later matches).
TEST(RouterTest, backtrack_cleans_params_from_failed_subtree) {
    Router router;
    http_handler null_handler = nullptr;
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/:id/posts/:pid", RouteConfig(null_handler)));
    ASSERT_TRUE(router.AddRoute(HttpMethod::kGet, "/users/*", RouteConfig(null_handler)));

    auto r = router.Match(HttpMethod::kGet, "/users/42/tags/zzz");
    ASSERT_TRUE(r.is_match);  // wildcard catches it
    EXPECT_TRUE(r.params.empty()) << "failed ':id' subtree leaked its binding to the wildcard match";
}

}  // namespace
}  // namespace http3
}  // namespace quicx