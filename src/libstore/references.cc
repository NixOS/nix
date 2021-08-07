#include "references.hh"
#include "hash.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <cstdlib>
#include <mutex>


namespace nix {


static unsigned int refLength = 32; /* characters */


static void search(const unsigned char * s, size_t len,
    StringSet & hashes, StringSet & seen)
{
    static std::once_flag initialised;
    static bool isBase32[256];
    std::call_once(initialised, [](){
        for (unsigned int i = 0; i < 256; ++i) isBase32[i] = false;
        for (unsigned int i = 0; i < base32Chars.size(); ++i)
            isBase32[(unsigned char) base32Chars[i]] = true;
    });

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

    void operator () (std::string_view data) override
    {
        /* It's possible that a reference spans the previous and current
           fragment, so search in the concatenation of the tail of the
           previous fragment and the start of the current fragment. */
        string s = tail + std::string(data, 0, refLength);
        search((const unsigned char *) s.data(), s.size(), hashes, seen);

        search((const unsigned char *) data.data(), data.size(), hashes, seen);

        size_t tailLen = data.size() <= refLength ? data.size() : refLength;
        tail = std::string(tail, tail.size() < refLength - tailLen ? 0 : tail.size() - (refLength - tailLen));
        tail.append({data.data() + data.size() - tailLen, tailLen});
    }
};


std::pair<PathSet, HashResult> scanForReferences(const string & path,
    const PathSet & refs)
{
    HashSink hashSink { htSHA256 };
    auto found = scanForReferences(hashSink, path, refs);
    auto hash = hashSink.finish();
    return std::pair<PathSet, HashResult>(found, hash);
}

PathSet scanForReferences(Sink & toTee,
    const string & path, const PathSet & refs)
{
    RefScanSink refsSink;
    TeeSink sink { refsSink, toTee };
    std::map<string, Path> backMap;

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

    return found;
}


RewritingSink::RewritingSink(const std::string & from, const std::string & to, Sink & nextSink)
    : from(from), to(to), nextSink(nextSink)
{
    assert(from.size() == to.size());
}

void RewritingSink::operator () (std::string_view data)
{
    std::string s(prev);
    s.append(data);

    size_t j = 0;
    while ((j = s.find(from, j)) != string::npos) {
        matches.push_back(pos + j);
        s.replace(j, from.size(), to);
    }

    prev = s.size() < from.size() ? s : std::string(s, s.size() - from.size() + 1, from.size() - 1);

    auto consumed = s.size() - prev.size();

    pos += consumed;

    if (consumed) nextSink(s.substr(0, consumed));
}

void RewritingSink::flush()
{
    if (prev.empty()) return;
    pos += prev.size();
    nextSink(prev);
    prev.clear();
}

HashModuloSink::HashModuloSink(HashType ht, const std::string & modulus)
    : hashSink(ht)
    , rewritingSink(modulus, std::string(modulus.size(), 0), hashSink)
{
}

void HashModuloSink::operator () (std::string_view data)
{
    rewritingSink(data);
}

HashResult HashModuloSink::finish()
{
    rewritingSink.flush();

    /* Hash the positions of the self-references. This ensures that a
       NAR with self-references and a NAR with some of the
       self-references already zeroed out do not produce a hash
       collision. FIXME: proof. */
    for (auto & pos : rewritingSink.matches)
        hashSink(fmt("|%d", pos));

    auto h = hashSink.finish();
    return {h.first, rewritingSink.pos};
}

}
