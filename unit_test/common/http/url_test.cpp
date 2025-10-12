#include "common/http/url.h"
#include <gtest/gtest.h>

namespace quicx {
namespace common {
namespace {

// ============================================================================
// URL Encode/Decode Tests
// ============================================================================

TEST(URLEncodeTest, BasicCharacters) {
    EXPECT_EQ(URLEncode("hello"), "hello");
    EXPECT_EQ(URLEncode("abc123"), "abc123");
}

TEST(URLEncodeTest, SpecialCharacters) {
    EXPECT_EQ(URLEncode("hello world"), "hello%20world");
    EXPECT_EQ(URLEncode("user@example.com"), "user%40example.com");
    EXPECT_EQ(URLEncode("a+b=c"), "a%2Bb%3Dc");
}

TEST(URLEncodeTest, UnreservedCharacters) {
    // RFC 3986: unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
    EXPECT_EQ(URLEncode("test-file_name.txt~"), "test-file_name.txt~");
}

TEST(URLEncodeTest, ChineseCharacters) {
    std::string chinese = "测试";
    std::string encoded = URLEncode(chinese);
    EXPECT_NE(encoded, chinese);  // Should be encoded
    EXPECT_TRUE(encoded.find('%') != std::string::npos);
}

TEST(URLEncodeTest, AllSpecialCharacters) {
    EXPECT_EQ(URLEncode(" "), "%20");
    EXPECT_EQ(URLEncode("!"), "%21");
    EXPECT_EQ(URLEncode("@"), "%40");
    EXPECT_EQ(URLEncode("#"), "%23");
    EXPECT_EQ(URLEncode("$"), "%24");
    EXPECT_EQ(URLEncode("%"), "%25");
    EXPECT_EQ(URLEncode("&"), "%26");
    EXPECT_EQ(URLEncode("="), "%3D");
    EXPECT_EQ(URLEncode("+"), "%2B");
}

TEST(URLDecodeTest, BasicDecoding) {
    EXPECT_EQ(URLDecode("hello%20world"), "hello world");
    EXPECT_EQ(URLDecode("user%40example.com"), "user@example.com");
}

TEST(URLDecodeTest, PlusToSpace) {
    EXPECT_EQ(URLDecode("hello+world"), "hello world");
    EXPECT_EQ(URLDecode("hello+world+test"), "hello world test");
}

TEST(URLDecodeTest, MixedEncoding) {
    EXPECT_EQ(URLDecode("hello+world%20test"), "hello world test");
}

TEST(URLDecodeTest, InvalidEncoding) {
    // Incomplete percent encoding - should keep as-is
    EXPECT_EQ(URLDecode("test%"), "test%");
    EXPECT_EQ(URLDecode("test%2"), "test%2");
}

TEST(URLDecodeTest, RoundTrip) {
    std::string original = "hello world!@#$%^&*()";
    std::string encoded = URLEncode(original);
    std::string decoded = URLDecode(encoded);
    EXPECT_EQ(original, decoded);
}

// ============================================================================
// Query String Tests
// ============================================================================

TEST(QueryStringTest, ParseSimple) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("key=value", params));
    EXPECT_EQ(params["key"], "value");
}

TEST(QueryStringTest, ParseMultiple) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("page=1&limit=10&sort=desc", params));
    EXPECT_EQ(params["page"], "1");
    EXPECT_EQ(params["limit"], "10");
    EXPECT_EQ(params["sort"], "desc");
}

TEST(QueryStringTest, ParseEmpty) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("", params));
    EXPECT_TRUE(params.empty());
}

TEST(QueryStringTest, ParseWithEncoding) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("q=hello%20world&email=test%40example.com", params));
    EXPECT_EQ(params["q"], "hello world");
    EXPECT_EQ(params["email"], "test@example.com");
}

TEST(QueryStringTest, ParseWithPlusSign) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("q=hello+world", params));
    EXPECT_EQ(params["q"], "hello world");
}

TEST(QueryStringTest, ParseKeyWithoutValue) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("debug&verbose", params));
    EXPECT_EQ(params["debug"], "");
    EXPECT_EQ(params["verbose"], "");
}

TEST(QueryStringTest, ParseMixedWithAndWithoutValues) {
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("page=1&debug&limit=10", params));
    EXPECT_EQ(params["page"], "1");
    EXPECT_EQ(params["debug"], "");
    EXPECT_EQ(params["limit"], "10");
}

TEST(QueryStringTest, SerializeSimple) {
    std::string result = SerializeQueryString({{"key", "value"}});
    EXPECT_EQ(result, "key=value");
}

TEST(QueryStringTest, SerializeMultiple) {
    auto result = SerializeQueryString({{"page", "1"}, {"limit", "10"}});
    // Order may vary, check both combinations
    EXPECT_TRUE(result == "page=1&limit=10" || result == "limit=10&page=1");
}

TEST(QueryStringTest, SerializeEmpty) {
    EXPECT_EQ(SerializeQueryString({}), "");
}

TEST(QueryStringTest, SerializeWithEncoding) {
    std::string result = SerializeQueryString({
        {"q", "hello world"},
        {"email", "test@example.com"}
    });
    EXPECT_TRUE(result.find("hello%20world") != std::string::npos);
    EXPECT_TRUE(result.find("test%40example.com") != std::string::npos);
}

TEST(QueryStringTest, RoundTrip) {
    std::unordered_map<std::string, std::string> original = {
        {"page", "1"},
        {"q", "hello world"},
        {"email", "test@example.com"}
    };
    
    std::string serialized = SerializeQueryString(original);
    std::unordered_map<std::string, std::string> parsed;
    EXPECT_TRUE(ParseQueryString(serialized, parsed));
    
    EXPECT_EQ(parsed["page"], "1");
    EXPECT_EQ(parsed["q"], "hello world");
    EXPECT_EQ(parsed["email"], "test@example.com");
}

// ============================================================================
// BuildAuthority Tests
// ============================================================================

TEST(AuthorityTest, WithNonDefaultPort) {
    EXPECT_EQ(BuildAuthority("example.com", 8080, "https"), "example.com:8080");
    EXPECT_EQ(BuildAuthority("localhost", 3000, "http"), "localhost:3000");
}

TEST(AuthorityTest, WithDefaultHTTPPort) {
    EXPECT_EQ(BuildAuthority("example.com", 80, "http"), "example.com");
}

TEST(AuthorityTest, WithDefaultHTTPSPort) {
    EXPECT_EQ(BuildAuthority("example.com", 443, "https"), "example.com");
}

TEST(AuthorityTest, WithZeroPort) {
    EXPECT_EQ(BuildAuthority("example.com", 0, "https"), "example.com");
}

TEST(AuthorityTest, IPv6) {
    EXPECT_EQ(BuildAuthority("::1", 8080, "https"), "::1:8080");
}

// ============================================================================
// BuildPathWithQuery Tests
// ============================================================================

TEST(PathWithQueryTest, PathOnly) {
    EXPECT_EQ(BuildPathWithQuery("/api/users", {}), "/api/users");
}

TEST(PathWithQueryTest, PathWithSingleQuery) {
    auto result = BuildPathWithQuery("/api/users", {{"page", "1"}});
    EXPECT_EQ(result, "/api/users?page=1");
}

TEST(PathWithQueryTest, PathWithMultipleQueries) {
    auto result = BuildPathWithQuery("/api/users", {{"page", "1"}, {"limit", "10"}});
    EXPECT_TRUE(result.find("/api/users?") == 0);
    EXPECT_TRUE(result.find("page=1") != std::string::npos);
    EXPECT_TRUE(result.find("limit=10") != std::string::npos);
}

TEST(PathWithQueryTest, EmptyPath) {
    auto result = BuildPathWithQuery("", {{"key", "value"}});
    EXPECT_EQ(result, "/?key=value");
}

TEST(PathWithQueryTest, PathWithoutLeadingSlash) {
    auto result = BuildPathWithQuery("api/users", {});
    EXPECT_EQ(result, "/api/users");
}

TEST(PathWithQueryTest, WithEncodedValues) {
    auto result = BuildPathWithQuery("/search", {{"q", "hello world"}});
    EXPECT_EQ(result, "/search?q=hello%20world");
}

// ============================================================================
// ParsePathWithQuery Tests
// ============================================================================

TEST(ParsePathWithQueryTest, PathOnly) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    
    EXPECT_TRUE(ParsePathWithQuery("/api/users", path, query));
    EXPECT_EQ(path, "/api/users");
    EXPECT_TRUE(query.empty());
}

TEST(ParsePathWithQueryTest, PathWithQuery) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    
    EXPECT_TRUE(ParsePathWithQuery("/api/users?page=1&limit=10", path, query));
    EXPECT_EQ(path, "/api/users");
    EXPECT_EQ(query["page"], "1");
    EXPECT_EQ(query["limit"], "10");
}

TEST(ParsePathWithQueryTest, PathWithEncodedQuery) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    
    EXPECT_TRUE(ParsePathWithQuery("/search?q=hello%20world&email=test%40example.com", path, query));
    EXPECT_EQ(path, "/search");
    EXPECT_EQ(query["q"], "hello world");
    EXPECT_EQ(query["email"], "test@example.com");
}

TEST(ParsePathWithQueryTest, EmptyString) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    
    EXPECT_FALSE(ParsePathWithQuery("", path, query));
}

TEST(ParsePathWithQueryTest, RootPath) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    
    EXPECT_TRUE(ParsePathWithQuery("/", path, query));
    EXPECT_EQ(path, "/");
    EXPECT_TRUE(query.empty());
}

TEST(ParsePathWithQueryTest, RoundTrip) {
    std::string original_path = "/api/users";
    std::unordered_map<std::string, std::string> original_query = {
        {"page", "1"},
        {"limit", "10"}
    };
    
    // Build
    std::string path_with_query = BuildPathWithQuery(original_path, original_query);
    
    // Parse
    std::string parsed_path;
    std::unordered_map<std::string, std::string> parsed_query;
    EXPECT_TRUE(ParsePathWithQuery(path_with_query, parsed_path, parsed_query));
    
    // Verify
    EXPECT_EQ(parsed_path, original_path);
    EXPECT_EQ(parsed_query["page"], "1");
    EXPECT_EQ(parsed_query["limit"], "10");
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(URLIntegrationTest, EdgeCases) {
    // Empty query value
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(ParseQueryString("key1=&key2=value", params));
    EXPECT_EQ(params["key1"], "");
    EXPECT_EQ(params["key2"], "value");
    
    // Multiple equals signs
    params.clear();
    EXPECT_TRUE(ParseQueryString("data=a=b=c", params));
    EXPECT_EQ(params["data"], "a=b=c");
}


// ============================================================================
// ParseURLForPseudoHeaders Tests (Client-side optimization)
// ============================================================================

TEST(ParseURLForPseudoHeadersTest, BasicHTTPS) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://example.com/api/users", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "https");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, 443);
    EXPECT_EQ(path_with_query, "/api/users");
}

TEST(ParseURLForPseudoHeadersTest, WithNonDefaultPort) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://api.example.com:8443/v1/endpoint", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "https");
    EXPECT_EQ(host, "api.example.com");
    EXPECT_EQ(port, 8443);
    EXPECT_EQ(path_with_query, "/v1/endpoint");
}

TEST(ParseURLForPseudoHeadersTest, WithQueryString) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://example.com/search?q=test&page=1", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "https");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, 443);
    // path_with_query includes the query string (NOT parsed into map)
    EXPECT_EQ(path_with_query, "/search?q=test&page=1");
}

TEST(ParseURLForPseudoHeadersTest, WithFragment) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://example.com/page?query=value#section", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "https");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, 443);
    // Fragment should be excluded (per RFC 9114)
    EXPECT_EQ(path_with_query, "/page?query=value");
    EXPECT_TRUE(path_with_query.find("#") == std::string::npos);
}

TEST(ParseURLForPseudoHeadersTest, HTTPDefaultPort) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("http://example.com/api", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "http");
    EXPECT_EQ(port, 80);
}

TEST(ParseURLForPseudoHeadersTest, RootPath) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://example.com", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(path_with_query, "/");
}

TEST(ParseURLForPseudoHeadersTest, PathWithoutLeadingSlash) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders("https://example.com/api", 
                                        scheme, host, port, path_with_query));
    
    EXPECT_EQ(path_with_query[0], '/');
}

TEST(ParseURLForPseudoHeadersTest, ComplexQueryString) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_TRUE(ParseURLForPseudoHeaders(
        "https://api.example.com:8080/search?q=hello world&filters[0]=active&sort=-created_at", 
        scheme, host, port, path_with_query));
    
    EXPECT_EQ(scheme, "https");
    EXPECT_EQ(host, "api.example.com");
    EXPECT_EQ(port, 8080);
    // Query string preserved as-is (encoding handled by caller if needed)
    EXPECT_TRUE(path_with_query.find("/search?") == 0);
    EXPECT_TRUE(path_with_query.find("q=hello world") != std::string::npos);
}

TEST(ParseURLForPseudoHeadersTest, EmptyURL) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_FALSE(ParseURLForPseudoHeaders("", scheme, host, port, path_with_query));
}

TEST(ParseURLForPseudoHeadersTest, NoScheme) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_FALSE(ParseURLForPseudoHeaders("example.com/api", scheme, host, port, path_with_query));
}

TEST(ParseURLForPseudoHeadersTest, InvalidPort) {
    std::string scheme, host, path_with_query;
    uint16_t port;
    
    EXPECT_FALSE(ParseURLForPseudoHeaders("https://example.com:invalid/api", 
                                         scheme, host, port, path_with_query));
}

TEST(URLErrorTest, ParsePathWithQueryEmpty) {
    std::string path;
    std::unordered_map<std::string, std::string> query;
    EXPECT_FALSE(ParsePathWithQuery("", path, query));
}

// ============================================================================
// Performance Tests (Basic)
// ============================================================================

TEST(URLPerformanceTest, EncodeDecodeMany) {
    // Test encoding/decoding performance with repeated operations
    for (int i = 0; i < 1000; ++i) {
        std::string test = "test" + std::to_string(i) + "@example.com";
        std::string encoded = URLEncode(test);
        std::string decoded = URLDecode(encoded);
        EXPECT_EQ(test, decoded);
    }
}

}
}
}