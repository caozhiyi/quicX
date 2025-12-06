#include <gtest/gtest.h>
#include "http3/router/util.h"

namespace quicx {
namespace http3 {
namespace {

TEST(router_util, parse_static) {
    std::string path = "/foo/bar";
    int offset = 0;

    std::string section = PathParse(path, offset);
    EXPECT_EQ(section, "/foo");
    EXPECT_EQ(offset, 4);

    section = PathParse(path, offset);
    EXPECT_EQ(section, "/bar");
    EXPECT_EQ(offset, 8);
}

TEST(router_util, parse_param) {
    std::string path = "/foo/:bar/baz";
    int offset = 0;

    std::string section = PathParse(path, offset);
    EXPECT_EQ(section, "/foo");
    EXPECT_EQ(offset, 4);

    section = PathParse(path, offset);
    EXPECT_EQ(section, "/:bar");
    EXPECT_EQ(offset, 9);

    section = PathParse(path, offset);
    EXPECT_EQ(section, "/baz");
    EXPECT_EQ(offset, 13);
}

TEST(router_util, parse_last) {
    std::string path = "/foo/";
    int offset = 0;

    std::string section = PathParse(path, offset);
    EXPECT_EQ(section, "/foo");
    EXPECT_EQ(offset, 4);

    section = PathParse(path, offset);
    EXPECT_EQ(section, "/");
    EXPECT_EQ(offset, 5);
}

}
}
}