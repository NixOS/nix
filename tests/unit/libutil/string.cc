#include <gtest/gtest.h>
#include "string.hh"

namespace nix {

TEST(string, isASCIILower) {
    ASSERT_TRUE(isASCIILower('a'));
    ASSERT_TRUE(isASCIILower('z'));
    ASSERT_FALSE(isASCIILower('A'));
    ASSERT_FALSE(isASCIILower('Z'));
    ASSERT_FALSE(isASCIILower('0'));
    ASSERT_FALSE(isASCIILower('9'));
    ASSERT_FALSE(isASCIILower(' '));
    ASSERT_FALSE(isASCIILower('\n'));
    ASSERT_FALSE(isASCIILower('\t'));
    ASSERT_FALSE(isASCIILower(':'));
}

TEST(string, isASCIIUpper) {
    ASSERT_FALSE(isASCIIUpper('a'));
    ASSERT_FALSE(isASCIIUpper('z'));
    ASSERT_TRUE(isASCIIUpper('A'));
    ASSERT_TRUE(isASCIIUpper('Z'));
    ASSERT_FALSE(isASCIIUpper('0'));
    ASSERT_FALSE(isASCIIUpper('9'));
    ASSERT_FALSE(isASCIIUpper(' '));
    ASSERT_FALSE(isASCIIUpper('\n'));
    ASSERT_FALSE(isASCIIUpper('\t'));
    ASSERT_FALSE(isASCIIUpper(':'));
}

TEST(string, isASCIIAlpha) {
    ASSERT_TRUE(isASCIIAlpha('a'));
    ASSERT_TRUE(isASCIIAlpha('z'));
    ASSERT_TRUE(isASCIIAlpha('A'));
    ASSERT_TRUE(isASCIIAlpha('Z'));
    ASSERT_FALSE(isASCIIAlpha('0'));
    ASSERT_FALSE(isASCIIAlpha('9'));
    ASSERT_FALSE(isASCIIAlpha(' '));
    ASSERT_FALSE(isASCIIAlpha('\n'));
    ASSERT_FALSE(isASCIIAlpha('\t'));
    ASSERT_FALSE(isASCIIAlpha(':'));
}

TEST(string, isASCIIDigit) {
    ASSERT_FALSE(isASCIIDigit('a'));
    ASSERT_FALSE(isASCIIDigit('z'));
    ASSERT_FALSE(isASCIIDigit('A'));
    ASSERT_FALSE(isASCIIDigit('Z'));
    ASSERT_TRUE(isASCIIDigit('0'));
    ASSERT_TRUE(isASCIIDigit('1'));
    ASSERT_TRUE(isASCIIDigit('9'));
    ASSERT_FALSE(isASCIIDigit(' '));
    ASSERT_FALSE(isASCIIDigit('\n'));
    ASSERT_FALSE(isASCIIDigit('\t'));
    ASSERT_FALSE(isASCIIDigit(':'));
}

}