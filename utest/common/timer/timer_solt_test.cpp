#include <gtest/gtest.h>

#include "common.h"
#include "common/timer/timer_interface.h"

TEST(timersolt_utest, index1) {
    TimerSoltIns t;
    t.SetCurIndex(25, quicx::TIT_MINUTE);

    uint16_t index = 0;
    uint16_t type = 0;

    t.GetCurIndex(index, type);

    EXPECT_EQ(25, index);
    EXPECT_EQ(quicx::TIT_MINUTE, type);
}

TEST(timersolt_utest, index2) {
    TimerSoltIns t;
    t.SetCurIndex(999, quicx::TIT_MILLISECOND);

    uint16_t index = 0;
    uint16_t type = 0;

    t.GetCurIndex(index, type);

    EXPECT_EQ(999, index);
    EXPECT_EQ(quicx::TIT_MILLISECOND, type);
}

TEST(timersolt_utest, index3) {
    TimerSoltIns t;
    t.SetCurIndex(59, quicx::TIT_SECOND);

    uint16_t index = 0;
    uint16_t type = 0;

    t.GetCurIndex(index, type);

    EXPECT_EQ(59, index);
    EXPECT_EQ(quicx::TIT_SECOND, type);
}