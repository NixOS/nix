#include <string>

#include "util.hh"


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

struct DumpSink 
{
    virtual void operator () (const unsigned char * data, unsigned int len) = 0;
};

void dumpPath(const Path & path, DumpSink & sink);


struct RestoreSource
{
    /* The callee should store exactly *len bytes in the buffer
       pointed to by data.  It should block if that much data is not
       yet available, or throw an error if it is not going to be
       available. */
    virtual void operator () (unsigned char * data, unsigned int len) = 0;
};

void restorePath(const Path & path, RestoreSource & source);
