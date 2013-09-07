#include "config.h"

#include <utility>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "util.hh"
#include "cgroups.hh"

namespace nix {


static Error cglineError(string::const_iterator from, string::const_iterator to) {
    return Error(format("not a valid cgroup line `%1%'") % string(from,to));
}

static string::const_iterator nextColon(string::const_iterator from,
        string::const_iterator i, string::const_iterator to)
{
    while (*i != ':') {
        if (i == to) throw cglineError(from, to);
        ++i;
    }
    return i;
}

static std::pair<string, string> procCgpath(string::const_iterator from, string::const_iterator to) {
    std::pair<string,string> out;
    string::const_iterator fst = nextColon(from, from, to);
    string::const_iterator snd = nextColon(from, fst + 1, to);
    out.first = string(fst + 1, snd);
    out.second = string(snd + 1, to);
    if (out.first.empty() || out.second.empty())
        throw cglineError(from, to);
    return out;
}

static bool goodCgroup(std::pair<string, string> cg) {
    return cg.first != "name=systemd" && cg.second != "/";
}

static string toCgPath(std::pair<string, string> cg) {
    return cg.first + ":" + cg.second;
}

Cgroups getCgroups(long pid) {
    string raw = readFile(pid == -1
            ? "/proc/self/cgroup"
            : (format("/proc/%1%/cgroup") % pid).str(), true); // drain -- stat reports size 0
    Cgroups cgroups;
    string::const_iterator lineStart = raw.begin();
    for (string::const_iterator i = raw.begin(); i != raw.end(); ++i ) {
        if (*i == '\n') {
            std::pair<string, string> pair = procCgpath(lineStart, i);
            if (goodCgroup(pair))
                cgroups.push_back(toCgPath(pair));
            lineStart = i + 1;
        }
    }
    if (lineStart != raw.end()) {
        std::pair<string, string> pair = procCgpath(lineStart, raw.end());
        if (goodCgroup(pair))
            cgroups.push_back(toCgPath(pair));
    }
    return cgroups;
}

string cut(int col, string source) {
    if (source.empty())
        throw Error("Empty string in cut");

    int from = 0, to = 0;
    int i = 0;
    while ( col >= 0 ) {
        from = i;
        while (i < int( source.size() ) && !isspace(source[i])) ++i;
        to = i;
        while (i < int( source.size() ) && isspace(source[i])) ++i;
        --col;
        if (col >= 0 && i == int( source.size()))
            throw Error("cut: string too short");
    }
    return source.substr(from, to - from);
}

static string cgToPath(string cg) {
    size_t split = cg.find(':');
    if ( split == string::npos || split == 0 )
        throw Error(format("Invalid cgroup path %1%") % cg);
    string controller = cg.substr(0, split);
    string group = cg.substr(split + 1);
    AutoCloseFD fd = open("/proc/mounts", O_RDONLY);
    if (fd == -1)
        throw SysError("opening `/proc/mounts'");
    while (true) {
        string line = readLine(fd);
        string dev = cut(0, line);
        if ( dev != "cgroup" )
            continue;
        string path = cut(1, line);
        string opts = cut(3, line);
        if ( opts.find(controller) != string::npos ) {
            return path + group; // group starts with slash
        }
    }
}

void joinCgroups(const Cgroups &cgs) {
    for (Cgroups::const_iterator i = cgs.begin(); i != cgs.end(); ++i ) {
        writeFile(cgToPath(*i) + "/tasks", (format("%1%") % getpid()).str());
    }
}


}
