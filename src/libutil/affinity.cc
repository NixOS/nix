#include "types.hh"
#include "util.hh"
#include "affinity.hh"

#if __linux__
#include <sched.h>
#endif

namespace nix {


#if __linux__
static bool didSaveAffinity = false;
static cpu_set_t savedAffinity;
#endif


void setAffinityTo(int cpu)
{
#if __linux__
    if (sched_getaffinity(0, sizeof(cpu_set_t), &savedAffinity) == -1) return;
    didSaveAffinity = true;
    debug(format("locking this thread to CPU %1%") % cpu);
    cpu_set_t newAffinity;
    CPU_ZERO(&newAffinity);
    CPU_SET(cpu, &newAffinity);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &newAffinity) == -1)
        printError(format("failed to lock thread to CPU %1%") % cpu);
#endif
}


int lockToCurrentCPU()
{
#if __linux__
    int cpu = sched_getcpu();
    if (cpu != -1) setAffinityTo(cpu);
    return cpu;
#else
    return -1;
#endif
}


void restoreAffinity()
{
#if __linux__
    if (!didSaveAffinity) return;
    if (sched_setaffinity(0, sizeof(cpu_set_t), &savedAffinity) == -1)
        printError("failed to restore affinity %1%");
#endif
}


}
