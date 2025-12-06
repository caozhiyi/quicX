#include <iostream>
#include <gtest/gtest.h>
#include "common/structure/linked_list.h"
#include "common/structure/linked_list_solt.h"

namespace quicx {
namespace common {
namespace {

static uint32_t kTestSharedCount = 0;

class TestListItem:
    public LinkedListSolt<TestListItem> {
public:
    TestListItem(){
        kTestSharedCount++;
    }
    ~TestListItem() {
        kTestSharedCount--;
    }

};

TEST(linked_list_utest, add1) {
    LinkedList<TestListItem> list;
    {
        auto item1 = std::make_shared<TestListItem>();
        list.PushBack(item1);
        EXPECT_EQ(kTestSharedCount, 1);

        auto item2 = std::make_shared<TestListItem>();
        list.PushBack(item2);
        EXPECT_EQ(kTestSharedCount, 2);
    }

    list.PopFront();
    EXPECT_EQ(kTestSharedCount, 1);

    list.PopFront();
    EXPECT_EQ(kTestSharedCount, 0);
}

TEST(linked_list_utest, add2) {
    LinkedList<TestListItem> list;
    {
        auto item1 = std::make_shared<TestListItem>();
        list.PushBack(item1);
        EXPECT_EQ(kTestSharedCount, 1);

        auto item2 = std::make_shared<TestListItem>();
        list.PushBack(item2);
        EXPECT_EQ(kTestSharedCount, 2);
    }

    list.Clear();
    EXPECT_EQ(kTestSharedCount, 0);
}

TEST(linked_list_utest, add3) {
    LinkedList<TestListItem> list;
    {
        auto item1 = std::make_shared<TestListItem>();
        list.PushBack(item1);
        EXPECT_EQ(kTestSharedCount, 1);

        auto item2 = std::make_shared<TestListItem>();
        list.PushBack(item2);
        EXPECT_EQ(kTestSharedCount, 2);

        auto item3 = std::make_shared<TestListItem>();
        list.PushBack(item3);
        EXPECT_EQ(kTestSharedCount, 3);
    }

    list.PopFront();
    EXPECT_EQ(kTestSharedCount, 2);

    list.PopFront();
    EXPECT_EQ(kTestSharedCount, 1);

    list.PopFront();
    EXPECT_EQ(kTestSharedCount, 0);
}

}
}
}