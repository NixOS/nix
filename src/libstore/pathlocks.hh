#pragma once
///@file

#include "file-descriptor.hh"

namespace nix {

class PathLocks
{
private:
#ifndef _WIN32
    typedef std::pair<int, Path> FDPair;
#else
    typedef std::pair<HANDLE, Path> FDPair;
#endif
    std::list<FDPair> fds;
    bool deletePaths;

public:
    PathLocks();
    PathLocks(const PathSet & paths,
        const std::string & waitMsg = "");
    bool lockPaths(const PathSet & _paths,
        const std::string & waitMsg = "",
        bool wait = true);
    ~PathLocks();
    void unlock();
    void setDeletion(bool deletePaths);
};

}

#include "pathlocks-impl.hh"
