#include <iostream>

extern "C" {
#include "md5.h"
#include "sha1.h"
}

#include "hash.hh"
#include "archive.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



Hash::Hash(HashType type)
{
    this->type = type;
    if (type == htMD5) hashSize = md5HashSize;
    else if (type == htSHA1) hashSize = sha1HashSize;
    else throw Error("unknown hash type");
    memset(hash, 0, hashSize);
}


bool Hash::operator == (const Hash & h2) const
{
    if (hashSize != h2.hashSize) return false;
    for (unsigned int i = 0; i < hashSize; i++)
        if (hash[i] != h2.hash[i]) return false;
    return true;
}


bool Hash::operator != (const Hash & h2) const
{
    return !(*this == h2);
}


bool Hash::operator < (const Hash & h) const
{
    for (unsigned int i = 0; i < hashSize; i++) {
        if (hash[i] < h.hash[i]) return true;
        if (hash[i] > h.hash[i]) return false;
    }
    return false;
}


Hash::operator string() const
{
    ostringstream str;
    for (unsigned int i = 0; i < hashSize; i++) {
        str.fill('0');
        str.width(2);
        str << hex << (int) hash[i];
    }
    return str.str();
}

    
Hash parseHash(const string & s)
{
    Hash hash(htMD5);
    if (s.length() != hash.hashSize * 2)
        throw Error(format("invalid hash `%1%'") % s);
    for (unsigned int i = 0; i < hash.hashSize; i++) {
        string s2(s, i * 2, 2);
        if (!isxdigit(s2[0]) || !isxdigit(s2[1])) 
            throw Error(format("invalid hash `%1%'") % s);
        istringstream str(s2);
        int n;
        str >> hex >> n;
        hash.hash[i] = n;
    }
    return hash;
}


bool isHash(const string & s)
{
    if (s.length() != 32) return false;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}


struct Ctx
{
    md5_ctx md5;
    sha_ctx sha1;
};


static void start(HashType ht, Ctx & ctx)
{
    if (ht == htMD5) md5_init_ctx(&ctx.md5);
    else if (ht == htSHA1) sha_init(&ctx.sha1);
}


static void update(HashType ht, Ctx & ctx,
    const unsigned char * bytes, unsigned int len)
{
    if (ht == htMD5) md5_process_bytes(bytes, len, &ctx.md5);
    else if (ht == htSHA1) sha_update(&ctx.sha1, bytes, len);
}


static void finish(HashType ht, Ctx & ctx, unsigned char * hash)
{
    if (ht == htMD5) md5_finish_ctx(&ctx.md5, hash);
    else if (ht == htSHA1) {
        sha_final(&ctx.sha1);
        sha_digest(&ctx.sha1, hash);
    }
}


Hash hashString(const string & s, HashType ht)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);
    update(ht, ctx, (const unsigned char *) s.c_str(), s.length());
    finish(ht, ctx, hash.hash);
    return hash;
}


Hash hashFile(const Path & path, HashType ht)
{
    Ctx ctx;
    Hash hash(ht);
    start(ht, ctx);

    AutoCloseFD fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) throw SysError(format("opening file `%1%'") % path);

    unsigned char buf[8192];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf)))) {
        checkInterrupt();
        if (n == -1) throw SysError(format("reading file `%1%'") % path);
        update(ht, ctx, buf, n);
    }
    
    finish(ht, ctx, hash.hash);
    return hash;
}


struct HashSink : DumpSink
{
    HashType ht;
    Ctx ctx;
    virtual void operator ()
        (const unsigned char * data, unsigned int len)
    {
        update(ht, ctx, data, len);
    }
};


Hash hashPath(const Path & path, HashType ht)
{
    HashSink sink;
    sink.ht = ht;
    Hash hash(ht);
    start(ht, sink.ctx);
    dumpPath(path, sink);
    finish(ht, sink.ctx, hash.hash);
    return hash;
}
