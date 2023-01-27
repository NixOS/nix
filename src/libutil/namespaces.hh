#pragma once

namespace nix {

#if __linux__

bool userNamespacesSupported();

bool mountNamespacesSupported();

bool pidNamespacesSupported();

#endif

}
