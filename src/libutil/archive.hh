#pragma once

#include "types.hh"
#include "serialise.hh"


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

struct PathFilter
{
    virtual ~PathFilter() { }
    virtual bool operator () (const Path & path) { return true; }
};

extern PathFilter defaultPathFilter;

void dumpPath(const Path & path, Sink & sink,
    PathFilter & filter = defaultPathFilter);

struct ParseSink
{
    virtual void createDirectory(const Path & path) { };

    virtual void createRegularFile(const Path & path) { };
    virtual void isExecutable() { };
    virtual void preallocateContents(unsigned long long size) { };
    virtual void receiveContents(unsigned char * data, unsigned int len) { };

    virtual void createSymlink(const Path & path, const string & target) { };
};

void parseDump(ParseSink & sink, Source & source);

void restorePath(const Path & path, Source & source);


// FIXME: global variables are bad m'kay.
extern bool useCaseHack;


}
