#include "ShopKeeperModel.h"

#include <gtest/gtest.h>

namespace NativeUI
{

TEST(ShopKeeperModel, conditionBuckets)
{
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(100), 0);
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(70), 0);
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(69), 1);
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(30), 1);
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(29), 2);
	EXPECT_EQ(ShopKeeperModel::ConditionBucket(0), 2);
}

TEST(ShopKeeperModel, slotClasses)
{
	EXPECT_EQ(ShopKeeperModel::SlotClass(true, false, false, ""), "sk-slot");
	EXPECT_EQ(ShopKeeperModel::SlotClass(false, false, false, ""), "sk-slot is-empty");
	EXPECT_EQ(ShopKeeperModel::SlotClass(true, true, false, ""), "sk-slot selected");
	EXPECT_EQ(ShopKeeperModel::SlotClass(true, false, true, ""), "sk-slot repaired");
	EXPECT_EQ(ShopKeeperModel::SlotClass(true, false, false, "jammed"), "sk-slot jammed");
	EXPECT_EQ(ShopKeeperModel::SlotClass(true, true, true, "jammed"), "sk-slot selected repaired jammed");
}

}
