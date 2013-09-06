#include "types.hh"
#include "util.hh"
#include "affinity.hh"

#if HAVE_SCHED_H
#include <sched.h>
#endif

namespace nix {


#if HAVE_SCHED_SETAFFINITY
static bool didSaveAffinity = false;
static cpu_set_t savedAffinity;
#endif


void setAffinityTo(int cpu)
{
#if HAVE_SCHED_SETAFFINITY
    if (sched_getaffinity(0, sizeof(cpu_set_t), &savedAffinity) == -1) return;
    didSaveAffinity = true;
    printMsg(lvlDebug, format("locking this thread to CPU %1%") % cpu);
    cpu_set_t newAffinity;
    CPU_ZERO(&newAffinity);
    CPU_SET(cpu, &newAffinity);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &newAffinity) == -1)
        printMsg(lvlError, format("failed to lock thread to CPU %1%") % cpu);
#endif
}


int lockToCurrentCPU()
{
#if HAVE_SCHED_SETAFFINITY
    int cpu = sched_getcpu();
    if (cpu != -1) setAffinityTo(cpu);
    return cpu;
#else
    return -1;
#endif
}


void restoreAffinity()
{
#if HAVE_SCHED_SETAFFINITY
    if (!didSaveAffinity) return;
    if (sched_setaffinity(0, sizeof(cpu_set_t), &savedAffinity) == -1)
        printMsg(lvlError, "failed to restore affinity %1%");
#endif
}


}
