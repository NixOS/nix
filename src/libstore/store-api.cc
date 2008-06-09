#include "store-api.hh"
#include "globals.hh"
#include "util.hh"


namespace nix {


bool StoreAPI::hasSubstitutes(const Path & path)
{
    PathSet paths = querySubstitutablePaths();
    return paths.find(path) != paths.end();
}


bool isInStore(const Path & path)
{
    return path[0] == '/'
        && string(path, 0, nixStore.size()) == nixStore
        && path.size() >= nixStore.size() + 2
        && path[nixStore.size()] == '/';
}


bool isStorePath(const Path & path)
{
    return isInStore(path)
        && path.find('/', nixStore.size() + 1) == Path::npos;
}


void assertStorePath(const Path & path)
{
    if (!isStorePath(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
}


Path toStorePath(const Path & path)
{
    if (!isInStore(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
    Path::size_type slash = path.find('/', nixStore.size() + 1);
    if (slash == Path::npos)
        return path;
    else
        return Path(path, 0, slash);
}


Path followLinksToStore(const Path & _path)
{
    Path path = absPath(_path);
    while (!isInStore(path)) {
        if (!isLink(path)) break;
        string target = readLink(path);
        path = absPath(target, dirOf(path));
    }
    if (!isInStore(path))
        throw Error(format("path `%1%' is not in the Nix store") % path);
    return path;
}


Path followLinksToStorePath(const Path & path)
{
    return toStorePath(followLinksToStore(path));
}


void checkStoreName(const string & name)
{
    string validChars = "+-._?=";
    /* Disallow names starting with a dot for possible security
       reasons (e.g., "." and ".."). */
    if (string(name, 0, 1) == ".")
        throw Error(format("illegal name: `%1%'") % name);
    for (string::const_iterator i = name.begin(); i != name.end(); ++i)
        if (!((*i >= 'A' && *i <= 'Z') ||
              (*i >= 'a' && *i <= 'z') ||
              (*i >= '0' && *i <= '9') ||
              validChars.find(*i) != string::npos))
        {
            throw Error(format("invalid character `%1%' in name `%2%'")
                % *i % name);
        }
}


Path makeStorePath(const string & type,
    const Hash & hash, const string & suffix)
{
    /* e.g., "source:sha256:1abc...:/nix/store:foo.tar.gz" */
    string s = type + ":sha256:" + printHash(hash) + ":"
        + nixStore + ":" + suffix;

    checkStoreName(suffix);

    return nixStore + "/"
        + printHash32(compressHash(hashString(htSHA256, s), 20))
        + "-" + suffix;
}


Path makeFixedOutputPath(bool recursive,
    string hashAlgo, Hash hash, string name)
{
    /* !!! copy/paste from primops.cc */
    Hash h = hashString(htSHA256, "fixed:out:"
        + (recursive ? (string) "r:" : "") + hashAlgo + ":"
        + printHash(hash) + ":"
        + "");
    return makeStorePath("output:out", h, name);
}


std::pair<Path, Hash> computeStorePathForPath(const Path & srcPath,
    bool fixed, bool recursive, string hashAlgo, PathFilter & filter)
{
    Hash h = hashPath(htSHA256, srcPath, filter);

    string baseName = baseNameOf(srcPath);

    Path dstPath;
    
    if (fixed) {
        HashType ht(parseHashType(hashAlgo));
        Hash h2 = recursive ? hashPath(ht, srcPath, filter) : hashFile(ht, srcPath);
        dstPath = makeFixedOutputPath(recursive, hashAlgo, h2, baseName);
    }
        
    else dstPath = makeStorePath("source", h, baseName);

    return std::pair<Path, Hash>(dstPath, h);
}


Path computeStorePathForText(const string & suffix, const string & s,
    const PathSet & references)
{
    Hash hash = hashString(htSHA256, s);
    /* Stuff the references (if any) into the type.  This is a bit
       hacky, but we can't put them in `s' since that would be
       ambiguous. */
    string type = "text";
    for (PathSet::const_iterator i = references.begin(); i != references.end(); ++i) {
        type += ":";
        type += *i;
    }
    return makeStorePath(type, hash, suffix);
}


/* Return a string accepted by decodeValidPathInfo() that
   registers the specified paths as valid.  Note: it's the
   responsibility of the caller to provide a closure. */
string makeValidityRegistration(const PathSet & paths,
    bool showDerivers, bool showHash)
{
    string s = "";
    
    for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
        s += *i + "\n";

        if (showHash)
            s += printHash(store->queryPathHash(*i)) + "\n";

        Path deriver = showDerivers ? store->queryDeriver(*i) : "";
        s += deriver + "\n";

        PathSet references;
        store->queryReferences(*i, references);

        s += (format("%1%\n") % references.size()).str();
            
        for (PathSet::iterator j = references.begin();
             j != references.end(); ++j)
            s += *j + "\n";
    }

    return s;
}


ValidPathInfo decodeValidPathInfo(std::istream & str, bool hashGiven)
{
    ValidPathInfo info;
    getline(str, info.path);
    if (str.eof()) { info.path = ""; return info; }
    if (hashGiven) {
        string s;
        getline(str, s);
        info.hash = parseHash(htSHA256, s);
    }
    getline(str, info.deriver);
    string s; int n;
    getline(str, s);
    if (!string2Int(s, n)) throw Error("number expected");
    while (n--) {
        getline(str, s);
        info.references.insert(s);
    }
    if (!str || str.eof()) throw Error("missing input");
    return info;
}


string showPaths(const PathSet & paths)
{
    string s;
    for (PathSet::const_iterator i = paths.begin();
         i != paths.end(); ++i)
    {
        if (s.size() != 0) s += ", ";
        s += "`" + *i + "'";
    }
    return s;
}


}


#include "local-store.hh"
#include "serialise.hh"
#include "remote-store.hh"


namespace nix {


boost::shared_ptr<StoreAPI> store;


boost::shared_ptr<StoreAPI> openStore()
{
    if (getEnv("NIX_REMOTE") == "")
        return boost::shared_ptr<StoreAPI>(new LocalStore());
    else
        return boost::shared_ptr<StoreAPI>(new RemoteStore());
}


}
