#include "values.hh"
#include "globals.hh"
#include "db.hh"


static void copyFile(string src, string dst)
{
    int res = system(("cat " + src + " > " + dst).c_str()); /* !!! escape */
    if (WEXITSTATUS(res) != 0)
        throw Error("cannot copy " + src + " to " + dst);
}


static string absValuePath(string s)
{
    return nixValues + "/" + s;
}


Hash addValue(string path)
{
    Hash hash = hashFile(path);

    string name;
    if (queryDB(nixDB, dbRefs, hash, name)) {
        debug((string) hash + " already known");
        return hash;
    }

    string baseName = baseNameOf(path);
    
    string targetName = (string) hash + "-" + baseName;

    copyFile(path, absValuePath(targetName));

    setDB(nixDB, dbRefs, hash, targetName);
    
    return hash;
}


#if 0
/* Download object referenced by the given URL into the sources
   directory.  Return the file name it was downloaded to. */
string fetchURL(string url)
{
    string filename = baseNameOf(url);
    string fullname = nixSourcesDir + "/" + filename;
    struct stat st;
    if (stat(fullname.c_str(), &st)) {
        cerr << "fetching " << url << endl;
        /* !!! quoting */
        string shellCmd =
            "cd " + nixSourcesDir + " && wget --quiet -N \"" + url + "\"";
        int res = system(shellCmd.c_str());
        if (WEXITSTATUS(res) != 0)
            throw Error("cannot fetch " + url);
    }
    return fullname;
}
#endif


string queryValuePath(Hash hash)
{
    bool checkedNet = false;

    while (1) {

        string name, url;

        if (queryDB(nixDB, dbRefs, hash, name)) {
            string fn = absValuePath(name);

            /* Verify that the file hasn't changed. !!! race */
            if (hashFile(fn) != hash)
                throw Error("file " + fn + " is stale");

            return fn;
        }

        throw Error("a file with hash " + (string) hash + " is requested, "
            "but it is not known to exist locally or on the network");
#if 0
        if (checkedNet)
            throw Error("consistency problem: file fetched from " + url + 
                " should have hash " + (string) hash + ", but it doesn't");

        if (!queryDB(nixDB, dbNetSources, hash, url))
            throw Error("a file with hash " + (string) hash + " is requested, "
                "but it is not known to exist locally or on the network");

        checkedNet = true;
        
        fn = fetchURL(url);
        
        setDB(nixDB, dbRefs, hash, fn);
#endif
    }
}
