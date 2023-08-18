#pragma once
///@file

namespace nix {

#if __linux__

bool userNamespacesSupported();

bool mountAndPidNamespacesSupported();

#endif

}
