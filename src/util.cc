#include "util.hh"


string thisSystem = SYSTEM;
string nixHomeDir = "/nix";
string nixHomeDirEnvVar = "NIX";



string absPath(string filename, string dir)
{
    if (filename[0] != '/') {
        if (dir == "") {
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf)))
                throw Error("cannot get cwd");
            dir = buf;
        }
        filename = dir + "/" + filename;
        /* !!! canonicalise */
        char resolved[PATH_MAX];
        if (!realpath(filename.c_str(), resolved))
            throw Error("cannot canonicalise path " + filename);
        filename = resolved;
    }
    return filename;
}


/* Return the directory part of the given path, i.e., everything
   before the final `/'. */
string dirOf(string s)
{
    unsigned int pos = s.rfind('/');
    if (pos == string::npos) throw Error("invalid file name");
    return string(s, 0, pos);
}


/* Return the base name of the given path, i.e., everything following
   the final `/'. */
string baseNameOf(string s)
{
    unsigned int pos = s.rfind('/');
    if (pos == string::npos) throw Error("invalid file name");
    return string(s, pos + 1);
}
