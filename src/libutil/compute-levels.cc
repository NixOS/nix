#include "types.hh"

#if HAVE_LIBCPUID
#include <libcpuid/libcpuid.h>
#include <map>
#endif

namespace nix {

#if HAVE_LIBCPUID

StringSet computeLevels() {
    StringSet levels;
    struct cpu_id_t data;

    const std::map<cpu_feature_level_t, std::string> feature_strings = {
        { FEATURE_LEVEL_X86_64_V1, "x86_64-v1" },
        { FEATURE_LEVEL_X86_64_V2, "x86_64-v2" },
        { FEATURE_LEVEL_X86_64_V3, "x86_64-v3" },
        { FEATURE_LEVEL_X86_64_V4, "x86_64-v4" },
    };

    if (cpu_identify(NULL, &data) < 0)
        return levels;

    for (cpu_feature_level_t level = FEATURE_LEVEL_X86_64_V1; level <= FEATURE_LEVEL_X86_64_V4; level++)
        if (data.feature_level >= level)
            levels.insert(feature_strings.at(level));

    return levels;
}

#else

StringSet computeLevels() {
    return StringSet{};
}

#endif // HAVE_LIBCPUID

}
