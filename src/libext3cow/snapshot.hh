#ifndef SNAPSHOT_H_
#define SNAPSHOT_H_

#include "types.hh"

namespace nix {

//unsigned int take_snapshot(const string & file_or_dir);
unsigned int take_snapshot(const string & dir);

}

#endif /*SNAPSHOT_H_*/
