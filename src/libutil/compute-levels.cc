#include "types.hh"

#if HAVE_LIBCPUID
#include <libcpuid/libcpuid.h>
#endif

namespace nix {

#if HAVE_LIBCPUID

StringSet computeLevels() {
    StringSet levels;

    if (!cpuid_present())
        return levels;

    cpu_raw_data_t raw;
    cpu_id_t data;

    if (cpuid_get_raw_data(&raw) < 0)
        return levels;

    if (cpu_identify(&raw, &data) < 0)
        return levels;

    if (!(data.flags[CPU_FEATURE_CMOV] &&
            data.flags[CPU_FEATURE_CX8] &&
            data.flags[CPU_FEATURE_FPU] &&
            data.flags[CPU_FEATURE_FXSR] &&
            data.flags[CPU_FEATURE_MMX] &&
            data.flags[CPU_FEATURE_SSE] &&
            data.flags[CPU_FEATURE_SSE2]))
        return levels;

    levels.insert("x86_64-v1");

    if (!(data.flags[CPU_FEATURE_CX16] &&
            data.flags[CPU_FEATURE_LAHF_LM] &&
            data.flags[CPU_FEATURE_POPCNT] &&
            // SSE3
            data.flags[CPU_FEATURE_PNI] &&
            data.flags[CPU_FEATURE_SSSE3] &&
            data.flags[CPU_FEATURE_SSE4_1] &&
            data.flags[CPU_FEATURE_SSE4_2]))
        return levels;

    levels.insert("x86_64-v2");

    if (!(data.flags[CPU_FEATURE_AVX] &&
            data.flags[CPU_FEATURE_AVX2] &&
            data.flags[CPU_FEATURE_F16C] &&
            data.flags[CPU_FEATURE_FMA3] &&
            // LZCNT
            data.flags[CPU_FEATURE_ABM] &&
            data.flags[CPU_FEATURE_MOVBE]))
        return levels;

    levels.insert("x86_64-v3");

    if (!(data.flags[CPU_FEATURE_AVX512F] &&
            data.flags[CPU_FEATURE_AVX512BW] &&
            data.flags[CPU_FEATURE_AVX512CD] &&
            data.flags[CPU_FEATURE_AVX512DQ] &&
            data.flags[CPU_FEATURE_AVX512VL]))
        return levels;

    levels.insert("x86_64-v4");

    return levels;
}

#else

StringSet computeLevels() {
    return StringSet{};
}

#endif // HAVE_LIBCPUID

}
