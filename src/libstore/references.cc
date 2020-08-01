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
        if (hashes.erase(ref)) {
            debug(format("found reference to '%1%' at offset '%2%'")
                  % ref % i);
            seen.insert(ref);
        }
        ++i;
    }
}


struct RefScanSink : Sink
{
    StringSet hashes;
    StringSet seen;

    string tail;

    RefScanSink() { }

    void operator () (const unsigned char * data, size_t len);
};


void RefScanSink::operator () (const unsigned char * data, size_t len)
{
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


std::pair<PathSet, HashResult> scanForReferences(const string & path,
    const PathSet & refs)
{
    RefScanSink refsSink;
    HashSink hashSink { htSHA256 };
    TeeSink sink { refsSink, hashSink };
    std::map<string, Path> backMap;

    /* For efficiency (and a higher hit rate), just search for the
       hash part of the file name.  (This assumes that all references
       have the form `HASH-bla'). */
    for (auto & i : refs) {
        auto baseName = std::string(baseNameOf(i));
        string::size_type pos = baseName.find('-');
        if (pos == string::npos)
            throw Error("bad reference '%1%'", i);
        string s = string(baseName, 0, pos);
        assert(s.size() == refLength);
        assert(backMap.find(s) == backMap.end());
        // parseHash(htSHA256, s);
        refsSink.hashes.insert(s);
        backMap[s] = i;
    }

    /* Look for the hashes in the NAR dump of the path. */
    dumpPath(path, sink);

    /* Map the hashes found back to their store paths. */
    PathSet found;
    for (auto & i : refsSink.seen) {
        std::map<string, Path>::iterator j;
        if ((j = backMap.find(i)) == backMap.end()) abort();
        found.insert(j->second);
    }

    auto hash = hashSink.finish();

    return std::pair<PathSet, HashResult>(found, hash);
}


RewritingSink::RewritingSink(const std::string & from, const std::string & to, Sink & nextSink)
    : from(from), to(to), nextSink(nextSink)
{
    assert(from.size() == to.size());
}

void RewritingSink::operator () (const unsigned char * data, size_t len)
{
    std::string s(prev);
    s.append((const char *) data, len);

    size_t j = 0;
    while ((j = s.find(from, j)) != string::npos) {
        matches.push_back(pos + j);
        s.replace(j, from.size(), to);
    }

    prev = s.size() < from.size() ? s : std::string(s, s.size() - from.size() + 1, from.size() - 1);

    auto consumed = s.size() - prev.size();

    pos += consumed;

    if (consumed) nextSink((unsigned char *) s.data(), consumed);
}

void RewritingSink::flush()
{
    if (prev.empty()) return;
    pos += prev.size();
    nextSink((unsigned char *) prev.data(), prev.size());
    prev.clear();
}

HashModuloSink::HashModuloSink(HashType ht, const std::string & modulus)
    : hashSink(ht)
    , rewritingSink(modulus, std::string(modulus.size(), 0), hashSink)
{
}

void HashModuloSink::operator () (const unsigned char * data, size_t len)
{
    rewritingSink(data, len);
}

HashResult HashModuloSink::finish()
{
    rewritingSink.flush();

    /* Hash the positions of the self-references. This ensures that a
       NAR with self-references and a NAR with some of the
       self-references already zeroed out do not produce a hash
       collision. FIXME: proof. */
    for (auto & pos : rewritingSink.matches) {
        auto s = fmt("|%d", pos);
        hashSink((unsigned char *) s.data(), s.size());
    }

    auto h = hashSink.finish();
    return {h.first, rewritingSink.pos};
}

}
