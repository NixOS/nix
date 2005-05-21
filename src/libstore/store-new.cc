#include "store-new.hh"

#include "util.hh"
#include "archive.hh"


const unsigned int pathHashLen = 32; /* characters */
const string nullPathHashRef(pathHashLen, 0);


PathHash::PathHash()
{
    rep = nullPathHashRef;
}


PathHash::PathHash(const Hash & h)
{
    assert(h.type == htSHA256);
    rep = printHash32(compressHash(h, 20));
}


string PathHash::toString() const
{
    return rep;
}


bool PathHash::isNull() const
{
    return rep == nullPathHashRef;
}


bool PathHash::operator ==(const PathHash & hash2) const
{
    return rep == hash2.rep;
}


bool PathHash::operator <(const PathHash & hash2) const
{
    return rep < hash2.rep;
}


struct CopySink : DumpSink
{
    string s;
    virtual void operator () (const unsigned char * data, unsigned int len)
    {
        s.append((const char *) data, len);
    }
};


struct CopySource : RestoreSource
{
    string & s;
    unsigned int pos;
    CopySource(string & _s) : s(_s), pos(0) { }
    virtual void operator () (unsigned char * data, unsigned int len)
    {
        s.copy((char *) data, len, pos);
        pos += len;
        assert(pos <= s.size());
    }
};


static string rewriteHashes(string s, const HashRewrites & rewrites,
    vector<int> & positions)
{
    for (HashRewrites::const_iterator i = rewrites.begin();
         i != rewrites.end(); ++i)
    {
        string from = i->first.toString(), to = i->second.toString();

        assert(from.size() == to.size());

        unsigned int j = 0;
        while ((j = s.find(from, j)) != string::npos) {
            debug(format("rewriting @ %1%") % j);
            positions.push_back(j);
            s.replace(j, to.size(), to);
        }
    }

    return s;
}


PathHash hashModulo(string s, const PathHash & modulus)
{
    vector<int> positions;
    
    if (!modulus.isNull()) {
        /* Zero out occurences of `modulus'. */
        HashRewrites rewrites;
        rewrites[modulus] = PathHash(); /* = null hash */
        s = rewriteHashes(s, rewrites, positions);
    }

    string positionPrefix;
    
    for (vector<int>::iterator i = positions.begin();
         i != positions.end(); ++i)
        positionPrefix += (format("|%1%") % *i).str();

    positionPrefix += "||";

    debug(format("positions %1%") % positionPrefix);
    
    return PathHash(hashString(htSHA256, positionPrefix + s));
}


Path addToStore(const Path & srcPath, const PathHash & selfHash)
{
    debug(format("adding %1%") % srcPath);

    CopySink sink;
    dumpPath(srcPath, sink);

    PathHash newHash = hashModulo(sink.s, selfHash);

    debug(format("newHash %1%") % newHash.toString());

    if (!selfHash.isNull()) {
        HashRewrites rewrites;
        rewrites[selfHash] = newHash;
        vector<int> positions;
        sink.s = rewriteHashes(sink.s, rewrites, positions);
        PathHash newHash2 = hashModulo(sink.s, newHash);
        assert(newHash2 == newHash);
        debug(format("newHash2 %1%") % newHash2.toString());
    }

    Path path = "./out/" + newHash.toString() + "-" + baseNameOf(srcPath);

    CopySource source(sink.s);
    restorePath(path, source);

    return path;
}


int main(int argc, char * * argv)
{
    verbosity = (Verbosity) ((int) 10);
    
    Path p = addToStore("./foo", PathHash(parseHash32(htSHA256, "8myr6ajc52b5sky7iplgz8jv703ljc0q")));

    cout << p << endl;
    
    return 0;
}
