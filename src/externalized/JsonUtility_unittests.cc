#include "gtest/gtest.h"

#include "Json.h"
#include "JsonUtility.h"

#include <string>

static bool contains(const ST::string& haystack, const char* needle)
{
	return haystack.to_std_string().find(needle) != std::string::npos;
}

TEST(JsonUtilityTest, parseListOfStrings)
{
	{
		auto strings = JsonUtility::parseJsonToListStrings("[]");
		ASSERT_TRUE(strings.has_value());
		ASSERT_EQ(strings->size(), 0u);
	}

	{
		auto strings = JsonUtility::parseJsonToListStrings("[\"foo\"]");
		ASSERT_TRUE(strings.has_value());
		ASSERT_EQ(strings->size(), 1u);
		ASSERT_STREQ((*strings)[0].c_str(), "foo");
	}

	{
		auto strings = JsonUtility::parseJsonToListStrings("[\"foo\", \"bar\"]");
		ASSERT_TRUE(strings.has_value());
		ASSERT_EQ(strings->size(), 2u);
		ASSERT_STREQ((*strings)[0].c_str(), "foo");
		ASSERT_STREQ((*strings)[1].c_str(), "bar");
	}
}

// Malformed input is a returned error now, not a thrown exception, and the
// error says what is wrong so a caller can act on it.
TEST(JsonUtilityTest, rejectsInvalidJson)
{
	auto strings = JsonUtility::parseJsonToListStrings("foo");
	ASSERT_FALSE(strings.has_value());
	EXPECT_TRUE(contains(strings.error(), "invalid JSON"));
}

TEST(JsonUtilityTest, rejectsNonArray)
{
	auto strings = JsonUtility::parseJsonToListStrings("{\"a\": 1}");
	ASSERT_FALSE(strings.has_value());
	EXPECT_TRUE(contains(strings.error(), "expected an array of strings"));
}

TEST(JsonUtilityTest, rejectsNonStringElement)
{
	auto strings = JsonUtility::parseJsonToListStrings("[\"foo\", 1]");
	ASSERT_FALSE(strings.has_value());
	EXPECT_TRUE(contains(strings.error(), "element 1"));
}

// parseListStrings takes a JsonValue directly; cover it without the JSON text
// round trip so the conversion and its errors are tested where they live.
TEST(JsonUtilityTest, parseListStringsFromValue)
{
	auto value = JsonValue::deserialize("[\"foo\", \"bar\"]");
	auto strings = JsonUtility::parseListStrings(value);
	ASSERT_TRUE(strings.has_value());
	ASSERT_EQ(strings->size(), 2u);
	ASSERT_STREQ((*strings)[0].c_str(), "foo");
	ASSERT_STREQ((*strings)[1].c_str(), "bar");

	auto notAnArray = JsonValue::deserialize("42");
	auto error = JsonUtility::parseListStrings(notAnArray);
	ASSERT_FALSE(error.has_value());
	EXPECT_TRUE(contains(error.error(), "expected an array of strings"));
}
