#include <algorithm>
#include <climits>
#include <fstream>
#include <iostream>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>

#include "comment.hh"
#include "nixexpr.hh"
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

struct Doc emptyDoc("", "");

/* parseDoc will try to recover a Doc by looking at the text that leads up to a
   term definition.*/
static struct Doc parseDoc(std::string sourcePrefix, const bool simple);

static std::string readSourceUpToPos(std::istream & source, const uint32_t line, const uint32_t col);

/* Consistent unindenting. It will only remove entire columns. */
static std::string unindent(std::string s);

static std::string trimUnindent(std::string s) { return trim(unindent(s)); }

static std::string readOriginUpToPos(const Pos &pos) {

  if (auto path = std::get_if<SourcePath>(&pos.origin)) {
    std::ifstream ifs(path->path.abs());
    return readSourceUpToPos(ifs, pos.line, pos.column);
  } else if (auto origin = std::get_if<Pos::String>(&pos.origin)) {
    auto source = origin->source->data();
     std::istringstream iss(source);
    return readSourceUpToPos(iss,pos.line,pos.column);
  } else {
    throw std::invalid_argument("This kind of pos.origin cannot be parsed yet.");
  }
}

static std::string readSourceUpToPos(std::istream & source, const uint32_t line, const uint32_t col){
    std::stringstream ret;
    size_t lineNum = 1;
    std::string currLine;
    while (getline(source, currLine) && lineNum <= line) {
      if (lineNum < line) {
        ret << currLine << "\n";
      } else if (lineNum == line) {
        ret << currLine.substr(0, col - 1);
      }
      lineNum++;
    }
    return ret.str();
}

struct Doc lookupDoc(const Pos &pos, const bool simple) {
  try {
    return parseDoc(readOriginUpToPos(pos), simple);
  } catch (std::exception &e) {
    ignoreException();
    return emptyDoc;
  }
}
/* Try to recover a Doc by looking at the text that leads up to a term
   definition */
static struct Doc parseDoc(std::string sourcePrefix, const bool simple) {

//   std::string spaces("(?:[ \\t]*)");
//   std::string lineComment("(?:[\\r\\n]*[^\\r\\n]*#" + spaces + "*)");
  std::string whitespaces("(?:[\\s]*)*");
  std::string ident("(?:[a-zA-Z0-9_'-][a-zA-Z_]*)");
  std::string path("(?:(?:" + whitespaces + ident + "\\." + whitespaces + ")*" +
                   ident + ")");
  std::string assign("(?:=" + whitespaces + ")");
  std::string lParen("(?:\\(*" + whitespaces + ")*");
  std::string lambda("(?:" + whitespaces + ":" + ident + lParen + ")*");
  std::string doc("([\\s]*\\/\\*(?:.|[\\s])*?(\\*\\*\\/))?");

  // 1. up all whitespaces
  // 2. eat remaining parenthesis
  // 3. skip all eventual outer lambdas (Limitation only simple arguments are suppported. e.g. a: NOT {b ? <c> }: )
  // 4. skip zero or one assignments to a path (Limitation only simple paths are suppported. e.g. a.b  NOT a.${b} )
  // 5. eat remaining whitespaces
  // 6. There should be the doc-comment
  std::string reverseRegex("^" + whitespaces + lParen + lambda +
                           "(?:" + assign + path + ")?" + whitespaces + doc);
  std::string simpleRegex("^" + whitespaces + doc);

  // The comment is located at the end of the file
  // Even with $ (Anchor End) regex starts to search from the beginning of
  // the file on large files this can cause regex stack overflow / recursion with more than 100k cycle steps.
  // with certain patterns causing the regex to step back and never reaching the end of the file.
  //
  // Solving this we search the comment in reverse order,
  // such that we can abort the search early. This is also significantly more
  // performant.

  // A high end solution would include access to the AST and a custom doc-comment parser,
  // because regex matching is very expensive.
  std::reverse(sourcePrefix.begin(), sourcePrefix.end());

  std::regex e(simpleRegex);
  if (!simple) {
    e = std::regex(reverseRegex);
  }

#define REGEX_GROUP_COMMENT 1

  std::smatch matches;
  regex_search(sourcePrefix, matches, e);

  std::stringstream buffer;
  if (matches.length() < REGEX_GROUP_COMMENT ||
      matches[REGEX_GROUP_COMMENT].str().empty()) {
    return emptyDoc;
  }

  std::string rawComment = matches[REGEX_GROUP_COMMENT];
  std::reverse(rawComment.begin(), rawComment.end());
  return Doc(rawComment, Doc::stripComment(rawComment));
}

std::string Doc::stripComment(std::string rawComment) {
  rawComment.erase(rawComment.find_last_not_of("\n") + 1);

  std::string s(trimUnindent(rawComment));
  auto suffixIdx = s.find("/**");
  if (suffixIdx != std::string::npos) {
    // Preserve indentation of content in the first line.
    // Writing directly after /**, without a leading newline is a potential
    // antipattern.
    s.replace(suffixIdx, 3, "   ");
  }
  // Remove the "*/"
  if (!s.empty() && *(--s.end()) == '/')
    s.pop_back();
  if (!s.empty() && *(--s.end()) == '*')
    s.pop_back();

  s = trimUnindent(s);
  return s;
}

static std::string unindent(std::string s) {
  size_t maxIndent = 1000;
  {
    std::istringstream inStream(s);
    for (std::string line; std::getline(inStream, line);) {
      size_t firstNonWS = line.find_first_not_of(" \t\r\n");
      if (firstNonWS != std::string::npos) {
        maxIndent = std::min(firstNonWS, maxIndent);
      }
    }
  }

  std::ostringstream unindentedStream;
  {
    std::istringstream inStream(s);
    for (std::string line; std::getline(inStream, line);) {
      if (line.length() >= maxIndent) {
        unindentedStream << line.substr(maxIndent) << std::endl;
      } else {
        unindentedStream << std::endl;
      }
    }
  }
  return unindentedStream.str();
}

} // namespace nix::Comment
