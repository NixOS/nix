#ifndef __PATHLOCKS_H
#define __PATHLOCKS_H

#include "util.hh"


class PathLocks 
{
private:
    list<int> fds;

public:
    PathLocks(const Strings & _paths);
    ~PathLocks();
};


#endif /* !__PATHLOCKS_H */
