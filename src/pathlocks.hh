#ifndef __PATHLOCKS_H
#define __PATHLOCKS_H

#include "util.hh"


class PathLocks 
{
private:
    list<int> fds;
    Paths paths;

public:
    PathLocks(const PathSet & _paths);
    ~PathLocks();
};


#endif /* !__PATHLOCKS_H */
