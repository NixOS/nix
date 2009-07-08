#include "eval.hh"
#include "util.hh"
#include "globals.hh"
#include "serialise.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {


void EvalState::loadNormalForms()
{
    //    nixCacheFile = getEnv("NIX_CACHE_FILE", nixStateDir + "/reduce-cache");
    nixCacheFile = getEnv("NIX_CACHE_FILE");
    string loadFlag = getEnv("NIX_CACHE_FILE_LOAD", "1");

    if(nixCacheFile == "" || loadFlag == "")
        return;
    if(!pathExists(nixCacheFile))
        return;

    printMsg(lvlInfo, format("Load cache: ..."));
    try {
        int fd = open(nixCacheFile.c_str(), O_RDONLY);
        if (fd == -1)
            throw SysError(format("opening file `%1%'") % nixCacheFile);
        AutoCloseFD auto_fd = fd;

        FdSource source = fd;
        normalForms = readATermMap(source);
    } catch (Error & e) {
        e.addPrefix(format("Cannot load cached reduce operations from %1%:\n") % nixCacheFile);
        throw;
    }
    printMsg(lvlInfo, format("Load cache: end"));
}

void EvalState::saveNormalForms()
{
    string saveFlag = getEnv("NIX_CACHE_FILE_SAVE", "");

    if(nixCacheFile == "" || saveFlag == "")
        return;

    printMsg(lvlInfo, format("Save cache: ..."));
    try {
        int fd = open(nixCacheFile.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (fd == -1)
            throw SysError(format("opening file `%1%'") % nixCacheFile);
        AutoCloseFD auto_fd = fd;

        FdSink sink = fd;
        writeATermMap(normalForms, sink);
    } catch (Error & e) {
        e.addPrefix(format("Cannot save cached reduce operations to %1%:\n") % nixCacheFile);
        throw;
    }
    printMsg(lvlInfo, format("Save cache: end"));
}


}
