#include "references.hh"
#include "hash.hh"
#include "util.hh"
#include "archive.hh"

#include <map>
#include <cstdlib>
#include <mutex>
#include <algorithm>


namespace nix {


static size_t refLength = 32; /* characters */


static void search(
    std::string_view s,
    StringSet & hashes,
    StringSet & seen)
{
    static std::once_flag initialised;
    static bool isBase32[256];
    std::call_once(initialised, [](){
        for (unsigned int i = 0; i < 256; ++i) isBase32[i] = false;
        for (unsigned int i = 0; i < base32Chars.size(); ++i)
            isBase32[(unsigned char) base32Chars[i]] = true;
    });

    for (size_t i = 0; i + refLength <= s.size(); ) {
        int j;
        bool match = true;
        for (j = refLength - 1; j >= 0; --j)
            if (!isBase32[(unsigned char) s[i + j]]) {
                i += j + 1;
                match = false;
                break;
            }
        if (!match) continue;
        std::string ref(s.substr(i, refLength));
        if (hashes.erase(ref)) {
            debug("found reference to '%1%' at offset '%2%'", ref, i);
            seen.insert(ref);
        }
        ++i;
    }
}


void RefScanSink::operator () (std::string_view data)
{
    /* It's possible that a reference spans the previous and current
       fragment, so search in the concatenation of the tail of the
       previous fragment and the start of the current fragment. */
    auto s = tail;
    auto tailLen = std::min(data.size(), refLength);
    s.append(data.data(), tailLen);
    search(s, hashes, seen);

    search(data, hashes, seen);

    auto rest = refLength - tailLen;
    if (rest < tail.size())
        tail = tail.substr(tail.size() - rest);
    tail.append(data.data() + data.size() - tailLen, tailLen);
}


RewritingSink::RewritingSink(const std::string & from, const std::string & to, Sink & nextSink)
    : RewritingSink({{from, to}}, nextSink)
{
}

RewritingSink::RewritingSink(const StringMap & rewrites, Sink & nextSink)
    : rewrites(rewrites), nextSink(nextSink)
{
    std::string::size_type maxRewriteSize = 0;
    for (auto & [from, to] : rewrites) {
        assert(from.size() == to.size());
        maxRewriteSize = std::max(maxRewriteSize, from.size());
    }
    this->maxRewriteSize = maxRewriteSize;
}

void RewritingSink::operator () (std::string_view data)
{
    std::string s(prev);
    s.append(data);

    s = rewriteStrings(s, rewrites);

    prev = s.size() < maxRewriteSize
        ? s
        : maxRewriteSize == 0
            ? ""
            : std::string(s, s.size() - maxRewriteSize + 1, maxRewriteSize - 1);

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
