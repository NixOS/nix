#pragma once

namespace nix {

void setAffinityTo(int cpu);
int lockToCurrentCPU();
void restoreAffinity();

}
