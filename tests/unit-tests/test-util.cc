#include "../../src/libutil/util.hh"
#include "../../src/libutil/types.hh"

#include <gtest/gtest.h>

namespace nix {

    /* ----------------------------------------------------------------------------
     * absPath
     * --------------------------------------------------------------------------*/

    TEST(absPath, doesntChangeRoot) {
        auto p = absPath("/");

        ASSERT_STREQ(p.c_str(), "/");
    }

    TEST(absPath, turnsEmptyPathIntoCWD) {
        char cwd[PATH_MAX+1];
        auto p = absPath("");

        ASSERT_STREQ(p.c_str(), getcwd((char*)&cwd, PATH_MAX));
    }

    TEST(absPath, usesOptionalBasePathWhenGiven) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd((char*)&_cwd, PATH_MAX);

        auto p = absPath("", cwd);

        ASSERT_STREQ(p.c_str(), cwd);
    }

    TEST(absPath, isIdempotent) {
        char _cwd[PATH_MAX+1];
        char* cwd = getcwd((char*)&_cwd, PATH_MAX);
        auto p1 = absPath(cwd);
        auto p2 = absPath(p1);

        ASSERT_STREQ(p1.c_str(), p2.c_str());
    }


    TEST(absPath, pathIsCanonicalised) {
        auto path = "/some/path/with/trailing/dot/.";
        auto p1 = absPath(path);
        auto p2 = absPath(p1);

        ASSERT_STREQ(p1.c_str(), "/some/path/with/trailing/dot");
        ASSERT_STREQ(p1.c_str(), p2.c_str());
    }

    /* ----------------------------------------------------------------------------
     * canonPath
     * --------------------------------------------------------------------------*/

    TEST(canonPath, removesTrailingSlashes) {
        auto path = "/this/is/a/path//";
        auto p = canonPath(path);

        ASSERT_STREQ(p.c_str(), "/this/is/a/path");
    }

    TEST(canonPath, removesDots) {
        auto path = "/this/./is/a/path/./";
        auto p = canonPath(path);

        ASSERT_STREQ(p.c_str(), "/this/is/a/path");
    }

    TEST(canonPath, removesDots2) {
        auto path = "/this/a/../is/a////path/foo/..";
        auto p = canonPath(path);

        ASSERT_STREQ(p.c_str(), "/this/is/a/path");
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

    // XXX: according to the doc of `dirOf`, dirOf("/") and dirOf("/foo")
    // should both return "" but it actually returns "/" in both cases
    TEST(dirOf, DISABLED_returnsEmptyStringForRoot) {
        auto p = dirOf("/");

        ASSERT_STREQ(p.c_str(), "");
    }

    TEST(dirOf, returnsFirstPathComponent) {
        auto p1 = dirOf("/dir/");
        ASSERT_STREQ(p1.c_str(), "/dir");
        auto p2 = dirOf("/dir");
        ASSERT_STREQ(p2.c_str(), "/");
        auto p3 = dirOf("/dir/..");
        ASSERT_STREQ(p3.c_str(), "/dir");
        auto p4 = dirOf("/dir/../");
        ASSERT_STREQ(p4.c_str(), "/dir/..");
    }

    /* ----------------------------------------------------------------------------
     * baseNameOf
     * --------------------------------------------------------------------------*/

    TEST(baseNameOf, emptyPath) {
        auto p1 = baseNameOf("");
        ASSERT_STREQ(std::string(p1).c_str(), "");
    }

    TEST(baseNameOf, pathOnRoot) {
        auto p1 = baseNameOf("/dir");
        ASSERT_STREQ(std::string(p1).c_str(), "dir");
    }

    TEST(baseNameOf, relativePath) {
        auto p1 = baseNameOf("dir/foo");
        ASSERT_STREQ(std::string(p1).c_str(), "foo");
    }

    TEST(baseNameOf, pathWithTrailingSlashRoot) {
        auto p1 = baseNameOf("/");
        ASSERT_STREQ(std::string(p1).c_str(), "");
    }

    // XXX: according to the doc of `baseNameOf`, baseNameOf("/dir/") should return
    // "" but it actually returns "dir"
    TEST(baseNameOf, DISABLED_trailingSlash) {
        auto p1 = baseNameOf("/dir/");
        ASSERT_STREQ(std::string(p1).c_str(), "");
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
        ASSERT_STREQ(base64Encode("").c_str(), "");
    }

    TEST(base64Encode, encodesAString) {
        ASSERT_STREQ(base64Encode("quod erat demonstrandum").c_str(), "cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0=");
    }

    TEST(base64Encode, encodeAndDecode) {
        auto s = "quod erat demonstrandum";
        auto encoded = base64Encode(s);
        auto decoded = base64Decode(encoded);

        ASSERT_STREQ(decoded.c_str(), s);
    }

    /* ----------------------------------------------------------------------------
     * base64Decode
     * --------------------------------------------------------------------------*/

    TEST(base64Decode, emptyString) {
        ASSERT_STREQ(base64Decode("").c_str(), "");
    }

    TEST(base64Decode, decodeAString) {
        ASSERT_STREQ(base64Decode("cXVvZCBlcmF0IGRlbW9uc3RyYW5kdW0=").c_str(), "quod erat demonstrandum");
    }

    /* ----------------------------------------------------------------------------
     * toLower
     * --------------------------------------------------------------------------*/

    TEST(toLower, emptyString) {
        ASSERT_STREQ(toLower("").c_str(), "");
    }

    TEST(toLower, nonLetters) {
        auto s = "!@(*$#)(@#=\\234_";
        ASSERT_STREQ(toLower(s).c_str(), s);
    }

    // XXX: std::tolower() doesn't cover this. This probably doesn't matter
    // since the context is paths and store paths specifically where such
    // characters are not allowed.
    TEST(toLower, DISABLED_umlauts) {
        auto s = "ÄÖÜ";
        ASSERT_STREQ(toLower(s).c_str(), "äöü");
    }

    /* ----------------------------------------------------------------------------
     * string2Float
     * --------------------------------------------------------------------------*/

    TEST(string2Float, emptyString) {
        double n;
        ASSERT_EQ(string2Float("", n), false);
    }

    TEST(string2Float, trivialConversions) {
        double n;
        ASSERT_EQ(string2Float("1.0", n), true);
        ASSERT_EQ(n, 1.0);

        ASSERT_EQ(string2Float("0.0", n), true);
        ASSERT_EQ(n, 0.0);

        ASSERT_EQ(string2Float("-100.25", n), true);
        ASSERT_EQ(n, (-100.25));
    }

    /* ----------------------------------------------------------------------------
     * string2Int
     * --------------------------------------------------------------------------*/

    TEST(string2Int, emptyString) {
        double n;
        ASSERT_EQ(string2Int("", n), false);
    }

    TEST(string2Int, trivialConversions) {
        double n;
        ASSERT_EQ(string2Int("1", n), true);
        ASSERT_EQ(n, 1);

        ASSERT_EQ(string2Int("0", n), true);
        ASSERT_EQ(n, 0);

        ASSERT_EQ(string2Int("-100", n), true);
        ASSERT_EQ(n, (-100));
    }

    /* ----------------------------------------------------------------------------
     * statusOk
     * --------------------------------------------------------------------------*/

    TEST(statusOk, zeroIsOk) {
        ASSERT_EQ(statusOk(0), true);
        ASSERT_EQ(statusOk(1), false);
    }

    /* ----------------------------------------------------------------------------
     * replaceInSet : XXX: this function isn't called anywhere!
     * --------------------------------------------------------------------------*/


    /* ----------------------------------------------------------------------------
     * rewriteStrings
     * --------------------------------------------------------------------------*/

    TEST(rewriteStrings, emptyString) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_STREQ(rewriteStrings("", rewrites).c_str(), "");
    }

    TEST(rewriteStrings, emptyRewrites) {
        StringMap rewrites;

        ASSERT_STREQ(rewriteStrings("this and that", rewrites).c_str(), "this and that");
    }

    TEST(rewriteStrings, successfulRewrite) {
        StringMap rewrites;
        rewrites["this"] = "that";

        ASSERT_STREQ(rewriteStrings("this and that", rewrites).c_str(), "that and that");
    }

    TEST(rewriteStrings, doesntOccur) {
        StringMap rewrites;
        rewrites["foo"] = "bar";

        ASSERT_STREQ(rewriteStrings("this and that", rewrites).c_str(), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * replaceStrings
     * --------------------------------------------------------------------------*/

    TEST(replaceStrings, emptyString) {
        ASSERT_STREQ(replaceStrings("", "this", "that").c_str(), "");
        ASSERT_STREQ(replaceStrings("this and that", "", "").c_str(), "this and that");
    }

    TEST(replaceStrings, successfulReplace) {
        ASSERT_STREQ(replaceStrings("this and that", "this", "that").c_str(), "that and that");
    }

    TEST(replaceStrings, doesntOccur) {
        ASSERT_STREQ(replaceStrings("this and that", "foo", "bar").c_str(), "this and that");
    }

    /* ----------------------------------------------------------------------------
     * trim
     * --------------------------------------------------------------------------*/

    TEST(trim, emptyString) {
        ASSERT_STREQ(trim("").c_str(), "");
    }

    TEST(trim, removesWhitespace) {
        ASSERT_STREQ(trim("foo").c_str(), "foo");
        ASSERT_STREQ(trim("     foo ").c_str(), "foo");
        ASSERT_STREQ(trim("     foo bar baz").c_str(), "foo bar baz");
        ASSERT_STREQ(trim("     \t foo bar baz\n").c_str(), "foo bar baz");
    }

    /* ----------------------------------------------------------------------------
     * chomp
     * --------------------------------------------------------------------------*/

    TEST(chomp, emptyString) {
        ASSERT_STREQ(chomp("").c_str(), "");
    }

    TEST(chomp, removesWhitespace) {
        ASSERT_STREQ(chomp("foo").c_str(), "foo");
        ASSERT_STREQ(chomp("foo ").c_str(), "foo");
        ASSERT_STREQ(chomp(" foo ").c_str(), " foo");
        ASSERT_STREQ(chomp(" foo bar baz  ").c_str(), " foo bar baz");
        ASSERT_STREQ(chomp("\t foo bar baz\n").c_str(), "\t foo bar baz");
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
        auto s = "";
        Strings expected = {  };

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

        ASSERT_STREQ(filterANSIEscapes(s).c_str(), expected);
    }

    TEST(filterANSIEscapes, doesntChangePrintableChars) {
        auto s = "09 2q304ruyhr slk2-19024 kjsadh sar f";

        ASSERT_STREQ(filterANSIEscapes(s).c_str(), s);
    }

    TEST(filterANSIEscapes, filtersColorCodes) {
        auto s = "\u001b[30m A \u001b[31m B \u001b[32m C \u001b[33m D \u001b[0m";

        ASSERT_STREQ(filterANSIEscapes(s, true, 2).c_str(), " A" );
        ASSERT_STREQ(filterANSIEscapes(s, true, 3).c_str(), " A " );
        ASSERT_STREQ(filterANSIEscapes(s, true, 4).c_str(), " A  " );
        ASSERT_STREQ(filterANSIEscapes(s, true, 5).c_str(), " A  B" );
        ASSERT_STREQ(filterANSIEscapes(s, true, 8).c_str(), " A  B  C" );
    }

    TEST(filterANSIEscapes, expandsTabs) {
        auto s = "foo\tbar\tbaz";

        ASSERT_STREQ(filterANSIEscapes(s, true).c_str(), "foo     bar     baz" );
    }

}
