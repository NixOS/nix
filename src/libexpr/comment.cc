#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <climits>
#include <algorithm>

#include "comment.hh"
#include "util.hh"

/* This module looks for documentation comments in the source code.

   Documentation is not retained during parsing, and it should not be,
   for performance reasons. Because of this the code has to jump
   through some hoops, to perform its task.

   Adapting the parser was not considered an option, so this code
   parses the comments from scratch, using regular expressions. These
   do not support all syntactic constructs, so in rare cases, they
   will fail and the code will report no documentation.

   One such situation is where documentation is requested for a
   partially applied function, where the outer lambda pattern
   matches an attribute set. This is not supported in the regexes
   because it potentially requires (almost?) the entire grammar.

   This module has been designed not to report the wrong
   documentation; considering that the wrong documentation is worse
   than no documentation. The regular expressions will only match
   simple, well understood syntactic structures, or not match at all.

   This approach to finding documentation does not cause extra runtime
   overhead, until used.

   This module does not support tab ('\t') characters. In some places
   they are treated as single spaces. They should be avoided.
*/
namespace nix::Comment {

struct Doc emptyDoc("", "", "", 0);

/* parseDoc will try to recover a Doc by looking at the text that leads up to a term
   definition.*/
static struct Doc parseDoc(std::string sourcePrefix);

/* stripComment unpacks a comment, by unindenting and stripping " * " prefixes as
   applicable. The argument should include any preceding whitespace. */
static std::string stripComment(std::string rawComment);

/* Consistent unindenting. It will only remove entire columns. */
static std::string unindent(std::string s);

static std::string trimUnindent(std::string s) {
    return trim(unindent(s));
}

static std::string stripPrefix(std::string prefix, std::string s) {
    std::string::size_type index = s.find(prefix);
    return (index == 0) ? s.erase(0, prefix.length()) : s;
}

static std::string readFileUpToPos(const Pos & pos) {
    if(auto path = std::get_if<SourcePath>(&pos.origin)) {
        std::ifstream ifs(path->path.abs());
        std::stringstream ret;
        size_t lineNum = 1;
        std::string line;

        while (getline(ifs, line) && lineNum <= pos.line) {
            if (lineNum < pos.line) {
                ret << line << "\n";
            } else if (lineNum == pos.line) {
                ret << line.substr(0, pos.column-1);
            }
            lineNum++;
        }

        return ret.str();
    } else {
        throw std::invalid_argument("pos.origin is not a path");
    }
}

struct Doc lookupDoc(const Pos & pos) {
    try {
        return parseDoc(readFileUpToPos(pos));
    } catch (std::exception & e) {
        ignoreException();
        return emptyDoc;
    }
}

/* See lambdas in parseDoc */
static int countLambdas(std::string piece) {
    return std::count(piece.begin(), piece.end(), ':');
}

/* Try to recover a Doc by looking at the text that leads up to a term
   definition */
static struct Doc parseDoc(std::string sourcePrefix) {

    std::string wss("[ \t\r\n]*");
    std::string spaces("[ \t]*");

    std::string singleLineComment(spaces + "#[^\r\n]*(?:\n|\r\n)");
    std::string multiSingleLineComment("(?:" + singleLineComment + ")*");
    std::string multiLineComment("\\/\\*(?:[^*]|\\*+[^*/])*\\*+\\/");
    std::string commentUnit("(" + multiSingleLineComment + "|" + spaces + multiLineComment + ")" + wss);

    std::string ident("[a-zA-Z_][a-zA-Z0-9_'-]*" + wss);
    std::string identKeep("([a-zA-Z_][a-zA-Z0-9_'-]*)" + wss);

    /* lvalue for nested attrset construction, but not matching
       quoted identifiers or ${...} or comments inbetween etc */
    std::string simplePath("(?:" + wss + ident + "\\.)*" + identKeep);

    std::string lambda(ident + wss + ":" + wss);

    /* see countLambdas */
    std::string lambdas("((:?" + lambda + ")*)");

    std::string assign("=" + wss);

    std::string re(commentUnit + simplePath + assign + lambdas + "$");
    std::regex e(re);

    #define REGEX_GROUP_COMMENT 1
    #define REGEX_GROUP_NAME 2
    #define REGEX_GROUP_LAMBDAS 3
    #define REGEX_GROUP_MAX 4

    std::smatch matches;
    regex_search(sourcePrefix, matches, e);

    std::stringstream buffer;
    if (matches.length() < REGEX_GROUP_MAX) {
        return emptyDoc;
    }

    std::string rawComment = matches[REGEX_GROUP_COMMENT];
    std::string name = matches[REGEX_GROUP_NAME];
    int timesApplied = countLambdas(matches[REGEX_GROUP_LAMBDAS]);
    return Doc(rawComment, stripComment(rawComment), name, timesApplied);
}

static std::string stripComment(std::string rawComment) {
    rawComment.erase(rawComment.find_last_not_of("\n")+1);

    std::string s(trimUnindent(rawComment));

    if (s[0] == '/' && s[1] == '*') {
        // Remove the "/*"
        // Indentation will be removed consistently later on
        s[0] = ' ';
        s[1] = ' ';

        // Remove the "*/"
        if (!s.empty() && *(--s.end()) == '/')
            s.pop_back();
        if (!s.empty() && *(--s.end()) == '*')
            s.pop_back();

        s = trimUnindent(s);

        std::istringstream inStream(s);
        std::ostringstream stripped;

        std::string line;

        /* at first, we assume a comment
         * that is formatted like this
         * with '*' characters at the beginning
         * of the line.
         */
        bool hasStars = true;

        while(std::getline(inStream,line,'\n')){
            if (hasStars && (
                    (!line.empty() && line[0] == '*')
                 || (line.length() >= 2 && line[0] == ' ' && line[1] == '*')
                    )) {
                if (line[0] == ' ') {
                    line = stripPrefix(" *", line);
                } else {
                    line = stripPrefix("*", line);
                }
            } else {
                hasStars = false;
            }

            stripped << line << std::endl;
        }
        return trimUnindent(stripped.str());
    }
    else {
        std::istringstream inStream(s);
        std::ostringstream stripped;

        std::string line;
        while(std::getline(inStream, line, '\n')) {
            line.erase(0, line.find("#") + 1);
            stripped << line << std::endl;
        }
        return trimUnindent(stripped.str());
    }
}

static std::string unindent(std::string s) {
    size_t maxIndent = 1000;
    {
        std::istringstream inStream(s);
        for (std::string line; std::getline(inStream, line); ) {
            size_t firstNonWS = line.find_first_not_of(" \t\r\n");
            if (firstNonWS != std::string::npos) {
                maxIndent = std::min(firstNonWS, maxIndent);
            }
        }
    }

    std::ostringstream unindentedStream;
    {
        std::istringstream inStream(s);
        for (std::string line; std::getline(inStream, line); ) {
            if (line.length() >= maxIndent) {
                unindentedStream << line.substr(maxIndent) << std::endl;
            } else {
                unindentedStream << std::endl;
            }
        }
    }
    return unindentedStream.str();
}

}
