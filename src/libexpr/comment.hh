#pragma once

#include "nixexpr.hh"

namespace nix::Comment {

struct Doc {
  std::string rawComment;
  std::string comment;

  // Number of times the curried function must be applied to get the value
  // that this structure documents.
  //
  // This is useful when showing the documentation for a partially applied
  // curried function. The documentation is for the unapplied function, so
  // this is crucial information.
  int timesApplied;

  /* stripComment unpacks a comment, by unindenting and stripping " * " prefixes
   as applicable. The argument should include any preceding whitespace. */
  static std::string stripComment(std::string rawComment);

  Doc(std::string rawComment, std::string comment) {
    this->rawComment = rawComment;
    this->comment = comment;
  }
  Doc(std::string rawComment, std::string comment, int timesApplied) {
    this->rawComment = rawComment;
    this->comment = comment;
    this->timesApplied = timesApplied;
  }
  Doc(std::string str) {
    this->rawComment = str;
    this->comment = Doc::stripComment(str);
  }
};

extern struct Doc emptyDoc;

// lookupDoc will try to recover a Doc. This will perform perform I/O,
// because documentation is not retained by the parser.
//
// Will return empty values if nothing can be found.
// For its limitations, see the docs of the implementation.
struct Doc lookupDoc(const Pos &pos, const bool simple);

} // namespace nix::Comment
