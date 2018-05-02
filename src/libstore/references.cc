#include "references.hh"
#include "hash.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <cstdlib>


namespace nix {


static unsigned int refLength = 32; /* characters */


static void search(const unsigned char * s, size_t len,
    StringSet & hashes, StringSet & seen)
{
    static bool initialised = false;
    static bool isBase32[256];
    if (!initialised) {
        for (unsigned int i = 0; i < 256; ++i) isBase32[i] = false;
        for (unsigned int i = 0; i < base32Chars.size(); ++i)
            isBase32[(unsigned char) base32Chars[i]] = true;
        initialised = true;
    }

    for (size_t i = 0; i + refLength <= len; ) {
        int j;
        bool match = true;
        for (j = refLength - 1; j >= 0; --j)
            if (!isBase32[(unsigned char) s[i + j]]) {
                i += j + 1;
                match = false;
                break;
            }
        if (!match) continue;
        string ref((const char *) s + i, refLength);
        if (hashes.find(ref) != hashes.end()) {
            debug(format("found reference to '%1%' at offset '%2%'")
                  % ref % i);
            seen.insert(ref);
            hashes.erase(ref);
        }
        ++i;
    }
}


struct RefScanSink : Sink
{
    HashSink hashSink;
    StringSet hashes;
    StringSet seen;

    string tail;

    RefScanSink() : hashSink(htSHA256) { }

    void operator () (const unsigned char * data, size_t len);
};


void RefScanSink::operator () (const unsigned char * data, size_t len)
{
    hashSink(data, len);

    /* It's possible that a reference spans the previous and current
       fragment, so search in the concatenation of the tail of the
       previous fragment and the start of the current fragment. */
    string s = tail + string((const char *) data, len > refLength ? refLength : len);
    search((const unsigned char *) s.data(), s.size(), hashes, seen);

    search(data, len, hashes, seen);

    size_t tailLen = len <= refLength ? len : refLength;
    tail =
        string(tail, tail.size() < refLength - tailLen ? 0 : tail.size() - (refLength - tailLen)) +
        string((const char *) data + len - tailLen, tailLen);
}


PathSet scanForReferences(const string & path,
    const PathSet & refs, HashResult & hash)
{
    RefScanSink sink;
    std::map<string, Path> backMap;

    /* For efficiency (and a higher hit rate), just search for the
       hash part of the file name.  (This assumes that all references
       have the form `HASH-bla'). */
    for (auto & i : refs) {
        string baseName = baseNameOf(i);
        string::size_type pos = baseName.find('-');
        if (pos == string::npos)
            throw Error(format("bad reference '%1%'") % i);
        string s = string(baseName, 0, pos);
        assert(s.size() == refLength);
        assert(backMap.find(s) == backMap.end());
        // parseHash(htSHA256, s);
        sink.hashes.insert(s);
        backMap[s] = i;
    }

    /* Look for the hashes in the NAR dump of the path. */
    dumpPath(path, sink);

    /* Map the hashes found back to their store paths. */
    PathSet found;
    for (auto & i : sink.seen) {
        std::map<string, Path>::iterator j;
        if ((j = backMap.find(i)) == backMap.end()) abort();
        found.insert(j->second);
    }

    hash = sink.hashSink.finish();

    return found;
}


}
