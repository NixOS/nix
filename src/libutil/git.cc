#include <cerrno>
#include <algorithm>
#include <vector>
#include <map>

#include <strings.h> // for strcasecmp

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "util.hh"
#include "config.hh"
#include "restore-sink.hh"

#include "git.hh"

namespace nix {


// Converts a Path to a ParseSink
void restoreGit(const Path & path, Source & source) {

    RestoreSink sink;
    sink.dstPath = path;
    parseDump(sink, source);

}

void parseGit(ParseSink & sink, Source & source) {
    parse(sink, source, "");
}

static void parse(ParseSink & sink, Source & source, const Path & path) {
    uint8_t buf[4];

    std::basic_string_view<uint8_t> buf_v {
        (const uint8_t *) & buf,
        std::size(buf)
    };
    source(buf_v);
    if (buf_v.compare((const uint8_t *)"blob")) {
        uint8_t space;
        source(& space, 1);
    }
    else {
    }
}

}
