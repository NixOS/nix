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

std::ostream& operator<<(std::ostream &os, const cpu_set_t &cset)
{
    auto count = CPU_COUNT(&cset);
    for (int i=0; i < count; ++i)
    {
        os << (CPU_ISSET(i,&cset) ? "1" : "0");
    }

    return os;
}
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
        printError("failed to lock thread to CPU %1%", cpu);
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
    {
        std::ostringstream oss;
        oss << savedAffinity;
        printError("failed to restore CPU affinity %1%", oss.str());
    }
#endif
}


}
