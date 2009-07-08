#include "serialise.hh"
#include "util.hh"
#include "aterm.hh"

#include <cstring>

namespace nix {


void FdSink::operator () (const unsigned char * data, unsigned int len)
{
    writeFull(fd, data, len);
}


void FdSource::operator () (unsigned char * data, unsigned int len)
{
    readFull(fd, data, len);
}


void writePadding(unsigned int len, Sink & sink)
{
    if (len % 8) {
        unsigned char zero[8];
        memset(zero, 0, sizeof(zero));
        sink(zero, 8 - (len % 8));
    }
}


void writeInt(unsigned int n, Sink & sink)
{
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    sink(buf, sizeof(buf));
}


void writeLongLong(unsigned long long n, Sink & sink)
{
    unsigned char buf[8];
    buf[0] = n & 0xff;
    buf[1] = (n >> 8) & 0xff;
    buf[2] = (n >> 16) & 0xff;
    buf[3] = (n >> 24) & 0xff;
    buf[4] = (n >> 32) & 0xff;
    buf[5] = (n >> 40) & 0xff;
    buf[6] = (n >> 48) & 0xff;
    buf[7] = (n >> 56) & 0xff;
    sink(buf, sizeof(buf));
}


void writeString(const string & s, Sink & sink)
{
    unsigned int len = s.length();
    writeInt(len, sink);
    sink((const unsigned char *) s.c_str(), len);
    writePadding(len, sink);
}


void writeStringSet(const StringSet & ss, Sink & sink)
{
    writeInt(ss.size(), sink);
    for (StringSet::iterator i = ss.begin(); i != ss.end(); ++i)
        writeString(*i, sink);
}


void writeATerm(ATerm t, Sink & sink)
{
    int len;
    unsigned char *buf = (unsigned char *) ATwriteToBinaryString(t, &len);
    AutoDeleteArray<unsigned char> d(buf);
    writeInt(len, sink);
    sink(buf, len);
}


/* convert the ATermMap to a list of couple because many terms are shared
   between the keys and between the values.  Thus the BAF stored by
   writeATerm consume less memory space.  The list of couples is saved
   inside a tree structure of /treedepth/ height because the serialiasation
   of ATerm cause a tail recurssion on list tails. */
void writeATermMap(const ATermMap & tm, Sink & sink)
{
    const unsigned int treedepth = 5;
    const unsigned int maxarity = 128; // 2 < maxarity < MAX_ARITY (= 255)
    const unsigned int bufsize = treedepth * maxarity;

    AFun map = ATmakeAFun("map", 2, ATfalse);
    AFun node = ATmakeAFun("node", maxarity, ATfalse);
    ATerm empty = (ATerm) ATmakeAppl0(ATmakeAFun("empty", 0, ATfalse));

    unsigned int c[treedepth];
    ATerm *buf = new ATerm[bufsize];
    AutoDeleteArray<ATerm> d(buf);

    memset(buf, 0, bufsize * sizeof(ATerm));
    ATprotectArray(buf, bufsize);

    for (unsigned int j = 0; j < treedepth; j++)
        c[j] = 0;

    for (ATermMap::const_iterator i = tm.begin(); i != tm.end(); ++i) {
         unsigned int depth = treedepth - 1;
         ATerm term = (ATerm) ATmakeAppl2(map, i->key, i->value);
         buf[depth * maxarity + c[depth]++] = term;
         while (c[depth] % maxarity == 0) {
             c[depth] = 0;
             term = (ATerm) ATmakeApplArray(node, &buf[depth * maxarity]);
             depth--;
             buf[depth * maxarity + c[depth]++] = term;
         }
    }

    unsigned int depth = treedepth;
    ATerm last_node = empty;
    while (depth--) {
        buf[depth * maxarity + c[depth]++] = last_node;
        while (c[depth] % maxarity)
            buf[depth * maxarity + c[depth]++] = empty;
        last_node = (ATerm) ATmakeApplArray(node, &buf[depth * maxarity]);
    }

    writeATerm(last_node, sink);
    ATunprotectArray(buf);
}


void readPadding(unsigned int len, Source & source)
{
    if (len % 8) {
        unsigned char zero[8];
        unsigned int n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw SerialisationError("non-zero padding");
    }
}


unsigned int readInt(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw SerialisationError("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}


unsigned long long readLongLong(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    return
        ((unsigned long long) buf[0]) |
        ((unsigned long long) buf[1] << 8) |
        ((unsigned long long) buf[2] << 16) |
        ((unsigned long long) buf[3] << 24) |
        ((unsigned long long) buf[4] << 32) |
        ((unsigned long long) buf[5] << 40) |
        ((unsigned long long) buf[6] << 48) |
        ((unsigned long long) buf[7] << 56);
}


string readString(Source & source)
{
    unsigned int len = readInt(source);
    unsigned char * buf = new unsigned char[len];
    AutoDeleteArray<unsigned char> d(buf);
    source(buf, len);
    readPadding(len, source);
    return string((char *) buf, len);
}

 
StringSet readStringSet(Source & source)
{
    unsigned int count = readInt(source);
    StringSet ss;
    while (count--)
        ss.insert(readString(source));
    return ss;
}


ATerm readATerm(Source & source)
{
    unsigned int len = readInt(source);
    unsigned char * buf = new unsigned char[len];
    AutoDeleteArray<unsigned char> d(buf);
    source(buf, len);
    ATerm t = ATreadFromBinaryString((char *) buf, len);
    if (t == 0)
        throw SerialisationError("cannot read a valid ATerm.");
    return t;
}


static void recursiveInitATermMap(ATermMap &tm, bool &stop, ATermAppl node)
{
    const unsigned int arity = ATgetArity(ATgetAFun(node));
    ATerm key, value;

    switch (arity) {
        case 0:
            stop = true;
            return;
        case 2:
            key = ATgetArgument(node, 0);
            value = ATgetArgument(node, 1);
            tm.set(key, value);
            return;
        default:
            for (unsigned int i = 0; i < arity && !stop; i++)
              recursiveInitATermMap(tm, stop, (ATermAppl) ATgetArgument(node, i));
            return;
    }
}

ATermMap readATermMap(Source & source)
{
    ATermMap tm;
    bool stop = false;

    recursiveInitATermMap(tm, stop, (ATermAppl) readATerm(source));
    return tm;
}


}
