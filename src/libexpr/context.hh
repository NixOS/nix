#pragma once

#include <cstring>

namespace nix {


struct ContextEntry
{
    const char *path;
    const char *output;
    bool discardOutputs;
};


struct CompareContextEntry
{
    inline bool operator() (const ContextEntry* const & c1, const ContextEntry* const & c2) {
        return (strcmp(c1->path,c2->path) < 0) || ((strcmp(c1->path,c2->path) == 0) && (strcmp(c1->output,c2->output) < 0));
    }
};


typedef std::set<const ContextEntry *, CompareContextEntry> ContextEntrySet;


}
