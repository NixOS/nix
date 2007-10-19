#include "serialise.hh"
#include "util.hh"


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

void writeBigUnsignedInt(unsigned int n, Sink & sink)
{
    writeString(unsignedInt2String(n), sink);		//TODO better way?
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

void writeIntVector(const IntVector & iv, Sink & sink)
{
    writeInt(iv.size(), sink);
    for(unsigned int i=0;i < iv.size(); i++)
        writeString(int2String(iv.at(i)), sink);		//TODO !!!!!!!!!!!!!!!!!!! writeInts ???????????
}

void writeRevisionClosure(const RevisionClosure & rc, Sink & sink)
{
    writeInt(rc.size(), sink);
    for (RevisionClosure::const_iterator i = rc.begin(); i != rc.end(); ++i){
    	writeString((*i).first, sink);
    	writeSnapshots((*i).second, sink);
    }
}

void writeSnapshots(const Snapshots & ss, Sink & sink)
{
    writeInt(ss.size(), sink);
    for (Snapshots::const_iterator i = ss.begin(); i != ss.end(); ++i){
    	writeString((*i).first, sink);
    	writeBigUnsignedInt((*i).second, sink);
    }
}

void writeRevisionClosureTS(const RevisionClosureTS & rc, Sink & sink)
{
	writeInt(rc.size(), sink);
    for (RevisionClosureTS::const_iterator i = rc.begin(); i != rc.end(); ++i){
    	writeString((*i).first, sink);
    	writeBigUnsignedInt((*i).second, sink);
    }
}

void writeRevisionInfos(const RevisionInfos & ri, Sink & sink)
{
	writeInt(ri.size(), sink);
    for (RevisionInfos::const_iterator i = ri.begin(); i != ri.end(); ++i){
    	writeBigUnsignedInt((*i).first, sink);
    	RevisionInfo rvi = (*i).second;
    	writeString(rvi.comment, sink);
    	writeBigUnsignedInt(rvi.timestamp, sink); 
    }
}

void writeCommitIntervals(const CommitIntervals ci, Sink & sink)
{
	writeInt(ci.size(), sink);
    for (CommitIntervals::const_iterator i = ci.begin(); i != ci.end(); ++i){
    	writeString((*i).first, sink);
    	writeBigUnsignedInt((*i).second, sink);
    }
}

////////////////

void readPadding(unsigned int len, Source & source)
{
    if (len % 8) {
        unsigned char zero[8];
        unsigned int n = 8 - (len % 8);
        source(zero, n);
        for (unsigned int i = 0; i < n; i++)
            if (zero[i]) throw Error("non-zero padding");
    }
}

unsigned int readInt(Source & source)
{
    unsigned char buf[8];
    source(buf, sizeof(buf));
    if (buf[4] || buf[5] || buf[6] || buf[7])
        throw Error("implementation cannot deal with > 32-bit integers");
    return
        buf[0] |
        (buf[1] << 8) |
        (buf[2] << 16) |
        (buf[3] << 24);
}

unsigned int readBigUnsignedInt(Source & source)
{
 	string s = readString(source);
 	unsigned int i;
 	if(! string2UnsignedInt(s, i) )
 		throw Error(format("Serialize: readBigUnsignedInt cannot read int: '%1%'") % s);
	return i;
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

IntVector readIntVector(Source & source)
{
    unsigned int count = readInt(source);
	IntVector iv;  
	while (count--){
		string s = readString(source);				//TODO !!!!!!!!!!!!!!!!!!!! readInt ???????????????
		int i;
		if (!string2Int(s, i))
            throw Error(format("`%1%' is corrupt in readIntVector") % s);
		iv.push_back(i);
	}
    return iv;
}

RevisionClosure readRevisionClosure(Source & source)
{
    unsigned int count = readInt(source);
    RevisionClosure rc;  
	while (count--){
    	string path = readString(source);
    	Snapshots ss = readSnapshots(source);
    	rc[path] = ss;
    }
    return rc;
}


Snapshots readSnapshots(Source & source)
{
    unsigned int count = readInt(source);
    Snapshots ss;  
	while (count--){
    	string path = readString(source);
    	unsigned int ri = readBigUnsignedInt(source);
    	ss[path] = ri;
    }
    return ss;
}


RevisionClosureTS readRevisionClosureTS(Source & source)
{
	unsigned int count = readInt(source);
    RevisionClosureTS rc;  
	while (count--){
    	string path = readString(source);
    	unsigned int ri = readBigUnsignedInt(source);
    	rc[path] = ri;
    }
    return rc;
}

RevisionInfos readRevisionInfos(Source & source)
{
	unsigned int count = readInt(source);
    RevisionInfos ri;  
	while (count--){
    	unsigned int revision = readBigUnsignedInt(source);
    	RevisionInfo rvi;
    	rvi.comment = readString(source);
    	rvi.timestamp = readBigUnsignedInt(source);
    	ri[revision] = rvi;
    }
    return ri;
}

CommitIntervals readCommitIntervals(Source & source)
{
	unsigned int count = readInt(source);
    CommitIntervals ci;  
	while (count--){
    	string path = readString(source);
    	unsigned int ri = readBigUnsignedInt(source);
    	ci[path] = ri;
    }
	return ci; 
}

}
