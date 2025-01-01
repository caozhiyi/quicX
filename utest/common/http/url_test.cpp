#include "common/http/url.h"
#include <gtest/gtest.h>

namespace quicx {
namespace common {
namespace {

TEST(URLTest, ParseURLComplete) {
    URL url;
    std::string url_str = "https://example.com:8080/path?query=value#fragment";
    EXPECT_TRUE(ParseURL(url_str, url));
    EXPECT_EQ(url.scheme, "https");
    EXPECT_EQ(url.host, "example.com");
    EXPECT_EQ(url.port, 8080);
    EXPECT_EQ(url.path, "/path");
    EXPECT_EQ(url.query["query"], "value");
    EXPECT_EQ(url.fragment, "fragment");
}

TEST(URLTest, ParseURLMissingComponents) {
    URL url;
    std::string url_str = "http://example.com";
    EXPECT_TRUE(ParseURL(url_str, url));
    EXPECT_EQ(url.scheme, "http");
    EXPECT_EQ(url.host, "example.com");
    EXPECT_EQ(url.port, 80);  // Default port for HTTP
    EXPECT_EQ(url.path, "");
    EXPECT_TRUE(url.query.empty());
    EXPECT_EQ(url.fragment, "");
}

TEST(URLTest, ParseURLInvalid) {
    URL url;
    std::string url_str = "invalid_url";
    EXPECT_FALSE(ParseURL(url_str, url));
}

TEST(URLTest, SerializeURLComplete) {
    URL url;
    url.scheme = "https";
    url.host = "example.com";
    url.port = 8080;
    url.path = "/path";
    url.query["query"] = "value";
    url.fragment = "fragment";
    std::string expected = "https://example.com:8080/path?query=value#fragment";
    EXPECT_EQ(SerializeURL(url), expected);
}

TEST(URLTest, SerializeURLMissingComponents) {
    URL url;
    url.scheme = "http";
    url.host = "example.com";
    std::string expected = "http://example.com";
    EXPECT_EQ(SerializeURL(url), expected);
}

TEST(URLTest, ParseURLMultipleQueryParams) {
    URL url;
    std::string url_str = "https://example.com/path?query1=value1&query2=value2";
    EXPECT_TRUE(ParseURL(url_str, url));
    EXPECT_EQ(url.query["query1"], "value1");
    EXPECT_EQ(url.query["query2"], "value2");
}

TEST(URLTest, ParseURLNoScheme) {
    URL url;
    std::string url_str = "//example.com/path";
    EXPECT_FALSE(ParseURL(url_str, url));
}

TEST(URLTest, ParseURLNoHost) {
    URL url;
    std::string url_str = "https:///path";
    EXPECT_TRUE(ParseURL(url_str, url));
    EXPECT_EQ(url.host, "");
    EXPECT_EQ(url.path, "/path");
}

TEST(URLTest, SerializeURLMultipleQueryParams) {
    URL url;
    url.scheme = "https";
    url.host = "example.com";
    url.path = "/path";
    url.query["query1"] = "value1";
    url.query["query2"] = "value2";
    std::string expected = "https://example.com/path?query2=value2&query1=value1";
    EXPECT_EQ(SerializeURL(url), expected);
}

TEST(URLTest, SerializeURLNoScheme) {
    URL url;
    url.host = "example.com";
    url.path = "/path";
    std::string expected = "example.com/path";
    EXPECT_EQ(SerializeURL(url), expected);
}

TEST(URLTest, SerializeURLNoHost) {
    URL url;
    url.scheme = "https";
    url.path = "/path";
    std::string expected = "https:///path";
    EXPECT_EQ(SerializeURL(url), expected);
}

TEST(URLTest, SerializeURLOnlyFragment) {
    URL url;
    url.fragment = "fragment";
    std::string expected = "#fragment";
    EXPECT_EQ(SerializeURL(url), expected);
}

}
}
}