#pragma once

#include "types.hh"
#include "serialise.hh"
#include "fs-sink.hh"

namespace nix {


/* dumpPath creates a Nix archive of the specified path.  The format
   is as follows:

   IF path points to a REGULAR FILE:
     dump(path) = attrs(
       [ ("type", "regular")
       , ("contents", contents(path))
       ])

   IF path points to a DIRECTORY:
     dump(path) = attrs(
       [ ("type", "directory")
       , ("entries", concat(map(f, sort(entries(path)))))
       ])
       where f(fn) = attrs(
         [ ("name", fn)
         , ("file", dump(path + "/" + fn))
         ])

   where:

     attrs(as) = concat(map(attr, as)) + encN(0)
     attrs((a, b)) = encS(a) + encS(b)

     encS(s) = encN(len(s)) + s + (padding until next 64-bit boundary)

     encN(n) = 64-bit little-endian encoding of n.

     contents(path) = the contents of a regular file.

     sort(strings) = lexicographic sort by 8-bit value (strcmp).

     entries(path) = the entries of a directory, without `.' and
     `..'.

     `+' denotes string concatenation. */


void dumpPath(const Path & path, Sink & sink,
    PathFilter & filter = defaultPathFilter);

void dumpString(const std::string & s, Sink & sink);

/* If the NAR archive contains a single file at top-level, then save
   the contents of the file to `s'.  Otherwise barf. */
struct RetrieveRegularNARSink : ParseSink
{
    bool regular = true;
    Sink & sink;

    RetrieveRegularNARSink(Sink & sink) : sink(sink) { }

    void createDirectory(const Path & path)
    {
        regular = false;
    }

    void receiveContents(unsigned char * data, size_t len)
    {
        sink(data, len);
    }

    void createSymlink(const Path & path, const string & target)
    {
        regular = false;
    }
};

void parseDump(ParseSink & sink, Source & source);

void restorePath(const Path & path, Source & source);

/* Read a NAR from 'source' and write it to 'sink'. */
void copyNAR(Source & source, Sink & sink);

void copyPath(const Path & from, const Path & to);


extern const std::string narVersionMagic1;


}
