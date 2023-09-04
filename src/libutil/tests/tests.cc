#include "util.hh"
#include "types.hh"

#include <limits.h>
#include <gtest/gtest.h>

#include <numeric>

namespace nix {

/* ----------- tests for util.hh ------------------------------------------------*/

    /* ----------------------------------------------------------------------------
     * absPath
     * --------------------------------------------------------------------------*/

    TEST(absPath, doesntChangeRoot) {
        auto p = absPath("/");

        ASSERT_EQ(p, "/");
    }




    TEST(absPath, turnsEmptyPathIntoCWD) {
        char cwd[PATH_MAX+1];
        auto p = absPath("");

        ASSERT_EQ(p, getcwd((char*)&cwd, PATH_MAX));
    }

    TEST(absPath, usesOptionalBasePathWhenGiven) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd((char*)&_cwd, PATH_MAX);

        auto p = absPath("", cwd);

        ASSERT_EQ(p, cwd);
    }

    TEST(absPath, isIdempotent) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd((char*)&_cwd, PATH_MAX);
        auto p1 = absPath(cwd);
        auto p2 = absPath(p1);

        ASSERT_EQ(p1, p2);
    }


    TEST(absPath, pathIsCanonicalised) {
        auto path = "/some/path/with/trailing/dot/.";
        auto p1 = absPath(path);
        auto p2 = absPath(p1);

        ASSERT_EQ(p1, "/some/path/with/trailing/dot");
        ASSERT_EQ(p1, p2);
    }

    /* ----------------------------------------------------------------------------
     * canonPath
     * --------------------------------------------------------------------------*/

    TEST(canonPath, removesTrailingSlashes) {
        auto path = "/this/is/a/path//";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, removesDots) {
        auto path = "/this/./is/a/path/./";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, removesDots2) {
        auto path = "/this/a/../is/a////path/foo/..";
        auto p = canonPath(path);

        ASSERT_EQ(p, "/this/is/a/path");
    }

    TEST(canonPath, requiresAbsolutePath) {
        ASSERT_ANY_THROW(canonPath("."));
        ASSERT_ANY_THROW(canonPath(".."));
        ASSERT_ANY_THROW(canonPath("../"));
        ASSERT_DEATH({ canonPath(""); }, "path != \"\"");
    }

    /* ----------------------------------------------------------------------------
     * dirOf
     * --------------------------------------------------------------------------*/

    TEST(dirOf, returnsEmptyStringForRoot) {
        auto p = dirOf("/");

        ASSERT_EQ(p, "/");
    }

    TEST(dirOf, returnsFirstPathComponent) {
        auto p1 = dirOf("/dir/");
        ASSERT_EQ(p1, "/dir");
        auto p2 = dirOf("/dir");
        ASSERT_EQ(p2, "/");
        auto p3 = dirOf("/dir/..");
        ASSERT_EQ(p3, "/dir");
        auto p4 = dirOf("/dir/../");
        ASSERT_EQ(p4, "/dir/..");
    }

    /* ----------------------------------------------------------------------------
     * baseNameOf
     * --------------------------------------------------------------------------*/

    TEST(baseNameOf, emptyPath) {
        auto p1 = baseNameOf("");
        ASSERT_EQ(p1, "");
    }

    TEST(baseNameOf, pathOnRoot) {
        auto p1 = baseNameOf("/dir");
        ASSERT_EQ(p1, "dir");
    }

    TEST(baseNameOf, relativePath) {
        auto p1 = baseNameOf("dir/foo");
        ASSERT_EQ(p1, "foo");
    }

    TEST(baseNameOf, pathWithTrailingSlashRoot) {
        auto p1 = baseNameOf("/");
        ASSERT_EQ(p1, "");
    }

    TEST(baseNameOf, trailingSlash) {
        auto p1 = baseNameOf("/dir/");
        ASSERT_EQ(p1, "dir");
    }

    /* ----------------------------------------------------------------------------
     * isInDir
     * --------------------------------------------------------------------------*/

    TEST(isInDir, trivialCase) {
        auto p1 = isInDir("/foo/bar", "/foo");
        ASSERT_EQ(p1, true);
    }

    TEST(isInDir, notInDir) {
        auto p1 = isInDir("/zes/foo/bar", "/foo");
        ASSERT_EQ(p1, false);
    }

    // XXX: hm, bug or feature? :) Looking at the implementation
    // this might be problematic.
    TEST(isInDir, emptyDir) {
        auto p1 = isInDir("/zes/foo/bar", "");
        ASSERT_EQ(p1, true);
    }

    /* ----------------------------------------------------------------------------
     * isDirOrInDir
     * --------------------------------------------------------------------------*/

    TEST(isDirOrInDir, trueForSameDirectory) {
        ASSERT_EQ(isDirOrInDir("/nix", "/nix"), true);
        ASSERT_EQ(isDirOrInDir("/", "/"), true);
    }

    TEST(isDirOrInDir, trueForEmptyPaths) {
        ASSERT_EQ(isDirOrInDir("", ""), true);
    }

    TEST(isDirOrInDir, falseForDisjunctPaths) {
        ASSERT_EQ(isDirOrInDir("/foo", "/bar"), false);
    }

    TEST(isDirOrInDir, relativePaths) {
        ASSERT_EQ(isDirOrInDir("/foo/..", "/foo"), true);
    }

    // XXX: while it is possible to use "." or ".." in the
    // first argument this doesn't seem to work in the second.
    TEST(isDirOrInDir, DISABLED_shouldWork) {
        ASSERT_EQ(isDirOrInDir("/foo/..", "/foo/."), true);

    }

    /* ----------------------------------------------------------------------------
     * pathExists
     * --------------------------------------------------------------------------*/

    TEST(pathExists, rootExists) {
        ASSERT_TRUE(pathExists("/"));
    }

    TEST(pathExists, cwdExists) {
        ASSERT_TRUE(pathExists("."));
    }

    TEST(pathExists, bogusPathDoesNotExist) {
        ASSERT_FALSE(pathExists("/home/schnitzel/darmstadt/pommes"));
    }

    /* ----------------------------------------------------------------------------
     * concatStringsSep
     * --------------------------------------------------------------------------*/

    TEST(concatStringsSep, buildCommaSeparatedString) {
        Strings strings;
        strings.push_back("this");
        strings.push_back("is");
        strings.push_back("great");

        ASSERT_EQ(concatStringsSep(",", strings), "this,is,great");
    }

    TEST(concatStringsSep, buildStringWithEmptySeparator) {
        Strings strings;
        strings.push_back("this");
        strings.push_back("is");
        strings.push_back("great");

        ASSERT_EQ(concatStringsSep("", strings), "thisisgreat");
    }

    TEST(concatStringsSep, buildSingleString) {
        Strings strings;
        strings.push_back("this");

        ASSERT_EQ(concatStringsSep(",", strings), "this");
    }

    /* ----------------------------------------------------------------------------
     * hasPrefix
     * --------------------------------------------------------------------------*/

    TEST(hasPrefix, emptyStringHasNoPrefix) {
        ASSERT_FALSE(hasPrefix("", "foo"));
    }

    TEST(hasPrefix, emptyStringIsAlwaysPrefix) {
        ASSERT_TRUE(hasPrefix("foo", ""));
        ASSERT_TRUE(hasPrefix("jshjkfhsadf", ""));
    }

    TEST(hasPrefix, trivialCase) {
        ASSERT_TRUE(hasPrefix("foobar", "foo"));
    }

    /* ----------------------------------------------------------------------------
     * hasSuffix
     * --------------------------------------------------------------------------*/

    TEST(hasSuffix, emptyStringHasNoSuffix) {
        ASSERT_FALSE(hasSuffix("", "foo"));
    }

    TEST(hasSuffix, trivialCase) {
        ASSERT_TRUE(hasSuffix("foo", "foo"));
        ASSERT_TRUE(hasSuffix("foobar", "bar"));
    }

    /* ----------------------------------------------------------------------------
     * base64Encode
     * --------------------------------------------------------------------------*/

    TEST(base64Encode, emptyString) {
        ASSERT_EQ(base64Encode(""), "");
    }

    TEST(base64Encode, encodesAString) {
        ASSERT_EQ(base64Encode("quod erat demonstrandum"), "cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0=");
    }

    TEST(base64Encode, encodeAndDecode) {
        auto s = "quod erat demonstrandum";
        auto encoded = base64Encode(s);
        auto decoded = base64Decode(encoded);

        ASSERT_EQ(decoded, s);
    }

    TEST(base64Encode, encodeAndDecodeNonPrintable) {
        char s[256];
        std::iota(std::rbegin(s), std::rend(s), 0);

        auto encoded = base64Encode(s);
        auto decoded = base64Decode(encoded);

        EXPECT_EQ(decoded.length(), 255);
        ASSERT_EQ(decoded, s);
    }

    /* ----------------------------------------------------------------------------
     * base64Decode
     * --------------------------------------------------------------------------*/

    TEST(base64Decode, emptyString) {
        ASSERT_EQ(base64Decode(""), "");
    }

    TEST(base64Decode, decodeAString) {
        ASSERT_EQ(base64Decode("cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0="), "quod erat demonstrandum");
    }

    TEST(base64Decode, decodeThrowsOnInvalidChar) {
        ASSERT_THROW(base64Decode("cXVvZCBlcm_0IGRlbW9uc3RyYW5kdW0="), Error);
    }

    /* ----------------------------------------------------------------------------
     * toLower
     * --------------------------------------------------------------------------*/

    TEST(toLower, emptyString) {
        ASSERT_EQ(toLower(""), "");
    }

    TEST(toLower, nonLetters) {
        auto s = "!@(*$#)(@#=\\234_";
        ASSERT_EQ(toLower(s), s);
    }

    // std::tolower() doesn't handle unicode characters. In the context of
    // store paths this isn't relevant but doesn't hurt to record this behavior
    // here.
    TEST(toLower, umlauts) {
        auto s = "ÄÖÜ";
        ASSERT_EQ(toLower(s), "ÄÖÜ");
    }

    /* ----------------------------------------------------------------------------
     * string2Float
     * --------------------------------------------------------------------------*/

    TEST(string2Float, emptyString) {
        ASSERT_EQ(string2Float<double>(""), std::nullopt);
    }

    TEST(string2Float, trivialConversions) {
        ASSERT_EQ(string2Float<double>("1.0"), 1.0);

        ASSERT_EQ(string2Float<double>("0.0"), 0.0);

        ASSERT_EQ(string2Float<double>("-100.25"), -100.25);
    }

    /* ----------------------------------------------------------------------------
     * string2Int
     * --------------------------------------------------------------------------*/

    TEST(string2Int, emptyString) {
        ASSERT_EQ(string2Int<int>(""), std::nullopt);
    }

    TEST(string2Int, trivialConversions) {
        ASSERT_EQ(string2Int<int>("1"), 1);

        ASSERT_EQ(string2Int<int>("0"), 0);

        ASSERT_EQ(string2Int<int>("-100"), -100);
    }

    /* ----------------------------------------------------------------------------
     * statusOk
     * --------------------------------------------------------------------------*/

    TEST(statusOk, zeroIsOk) {
        ASSERT_EQ(statusOk(0), true);
        ASSERT_EQ(statusOk(1), false);
    }


    /* ----------------------------------------------------------------------------
     * rewriteStrings
     * --------------------------------------------------------------------------*/

    TEST(rewriteStrings, emptyString) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_EQ(rewriteStrings("", rewrites), "");
    }

    TEST(rewriteStrings, emptyRewrites) {
        StringMap rewrites;

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
    }

    TEST(rewriteStrings, successfulRewrite) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "that and that");
    }

    TEST(rewriteStrings, doesntOccur) {
        StringMap rewrites;
        rewrites["foo"] = "bar";

        ASSERT_EQ(rewriteStrings("this and that", rewrites), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * replaceStrings
     * --------------------------------------------------------------------------*/

    TEST(replaceStrings, emptyString) {
        ASSERT_EQ(replaceStrings("", "this", "that"), "");
        ASSERT_EQ(replaceStrings("this and that", "", ""), "this and that");
    }

    TEST(replaceStrings, successfulReplace) {
        ASSERT_EQ(replaceStrings("this and that", "this", "that"), "that and that");
    }

    TEST(replaceStrings, doesntOccur) {
        ASSERT_EQ(replaceStrings("this and that", "foo", "bar"), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * trim
     * --------------------------------------------------------------------------*/

    TEST(trim, emptyString) {
        ASSERT_EQ(trim(""), "");
    }

    TEST(trim, removesWhitespace) {
        ASSERT_EQ(trim("foo"), "foo");
        ASSERT_EQ(trim("     foo "), "foo");
        ASSERT_EQ(trim("     foo bar baz"), "foo bar baz");
        ASSERT_EQ(trim("     \t foo bar baz\n"), "foo bar baz");
    }

    /* ----------------------------------------------------------------------------
     * chomp
     * --------------------------------------------------------------------------*/

    TEST(chomp, emptyString) {
        ASSERT_EQ(chomp(""), "");
    }

    TEST(chomp, removesWhitespace) {
        ASSERT_EQ(chomp("foo"), "foo");
        ASSERT_EQ(chomp("foo "), "foo");
        ASSERT_EQ(chomp(" foo "), " foo");
        ASSERT_EQ(chomp(" foo bar baz  "), " foo bar baz");
        ASSERT_EQ(chomp("\t foo bar baz\n"), "\t foo bar baz");
    }

    /* ----------------------------------------------------------------------------
     * quoteStrings
     * --------------------------------------------------------------------------*/

    TEST(quoteStrings, empty) {
        Strings s = { };
        Strings expected = { };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    TEST(quoteStrings, emptyStrings) {
        Strings s = { "", "", "" };
        Strings expected = { "''", "''", "''" };
        ASSERT_EQ(quoteStrings(s), expected);

    }

    TEST(quoteStrings, trivialQuote) {
        Strings s = { "foo", "bar", "baz" };
        Strings expected = { "'foo'", "'bar'", "'baz'" };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    TEST(quoteStrings, quotedStrings) {
        Strings s = { "'foo'", "'bar'", "'baz'" };
        Strings expected = { "''foo''", "''bar''", "''baz''" };

        ASSERT_EQ(quoteStrings(s), expected);
    }

    /* ----------------------------------------------------------------------------
     * tokenizeString
     * --------------------------------------------------------------------------*/

    TEST(tokenizeString, empty) {
        Strings expected = { };

        ASSERT_EQ(tokenizeString<Strings>(""), expected);
    }

    TEST(tokenizeString, tokenizeSpacesWithDefaults) {
        auto s = "foo bar baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsWithDefaults) {
        auto s = "foo\tbar\tbaz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesWithDefaults) {
        auto s = "foo\t bar\t baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesNewlineWithDefaults) {
        auto s = "foo\t\n bar\t\n baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);
    }

    TEST(tokenizeString, tokenizeTabsSpacesNewlineRetWithDefaults) {
        auto s = "foo\t\n\r bar\t\n\r baz";
        Strings expected = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s), expected);

        auto s2 = "foo \t\n\r bar \t\n\r baz";
        Strings expected2 = { "foo", "bar", "baz" };

        ASSERT_EQ(tokenizeString<Strings>(s2), expected2);
    }

    TEST(tokenizeString, tokenizeWithCustomSep) {
        auto s = "foo\n,bar\n,baz\n";
        Strings expected = { "foo\n", "bar\n", "baz\n" };

        ASSERT_EQ(tokenizeString<Strings>(s, ","), expected);
    }

    /* ----------------------------------------------------------------------------
     * get
     * --------------------------------------------------------------------------*/

    TEST(get, emptyContainer) {
        StringMap s = { };
        auto expected = std::nullopt;

        ASSERT_EQ(get(s, "one"), expected);
    }

    TEST(get, getFromContainer) {
        StringMap s;
        s["one"] = "yi";
        s["two"] = "er";
        auto expected = "yi";

        ASSERT_EQ(get(s, "one"), expected);
    }

    /* ----------------------------------------------------------------------------
     * filterANSIEscapes
     * --------------------------------------------------------------------------*/

    TEST(filterANSIEscapes, emptyString) {
        auto s = "";
        auto expected = "";

        ASSERT_EQ(filterANSIEscapes(s), expected);
    }

    TEST(filterANSIEscapes, doesntChangePrintableChars) {
        auto s = "09 2q304ruyhr slk2-19024 kjsadh sar f";

        ASSERT_EQ(filterANSIEscapes(s), s);
    }

    TEST(filterANSIEscapes, filtersColorCodes) {
        auto s = "\u001b[30m A \u001b[31m B \u001b[32m C \u001b[33m D \u001b[0m";

        ASSERT_EQ(filterANSIEscapes(s, true, 2), " A" );
        ASSERT_EQ(filterANSIEscapes(s, true, 3), " A " );
        ASSERT_EQ(filterANSIEscapes(s, true, 4), " A  " );
        ASSERT_EQ(filterANSIEscapes(s, true, 5), " A  B" );
        ASSERT_EQ(filterANSIEscapes(s, true, 8), " A  B  C" );
    }

    TEST(filterANSIEscapes, expandsTabs) {
        auto s = "foo\tbar\tbaz";

        ASSERT_EQ(filterANSIEscapes(s, true), "foo     bar     baz" );
    }

    TEST(filterANSIEscapes, utf8) {
        ASSERT_EQ(filterANSIEscapes("foobar", true, 5), "fooba");
        ASSERT_EQ(filterANSIEscapes("fóóbär", true, 6), "fóóbär");
        ASSERT_EQ(filterANSIEscapes("fóóbär", true, 5), "fóóbä");
        ASSERT_EQ(filterANSIEscapes("fóóbär", true, 3), "fóó");
        ASSERT_EQ(filterANSIEscapes("f€€bär", true, 4), "f€€b");
        ASSERT_EQ(filterANSIEscapes("f𐍈𐍈bär", true, 4), "f𐍈𐍈b");
    }

}
