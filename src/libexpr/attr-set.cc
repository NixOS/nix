#include "attr-set.hh"
#include "eval-inline.hh"

#include <algorithm>
#include <string.h>

namespace nix {

std::unordered_set<Bindings::Names, Bindings::NamesHasher>       * Bindings::pnamesTable = new std::unordered_set<Bindings::Names, Bindings::NamesHasher>      (0x4000, Bindings::NamesHasher());
std::unordered_set<std::vector<const Pos*>, Bindings::PosHasher> * Bindings::pposTable   = new std::unordered_set<std::vector<const Pos*>, Bindings::PosHasher>(0x4000, Bindings::PosHasher());

int Bindings::nNameLists = 0;
int Bindings::bNameLists = 0;
int Bindings::nPosLists = 0;
int Bindings::bPosLists = 0;


Bindings* BindingsBuilder::resultAt(void *p, bool alreadySorted) {
    size_t savedsize = size();

    if (!alreadySorted)
      quicksort(begin(), end());

#ifndef _NDEBUG
    resultCalled = true; // size(), begin(), end() do not work after `names` moved to Bindings::pnamesTable
#endif

    std::pair<std::unordered_set<Bindings::Names, Bindings::NamesHasher>::iterator, bool> res1 = Bindings::pnamesTable->emplace(std::move(names));
    if (res1.second) {
        Bindings::nNameLists ++;
        Bindings::bNameLists += savedsize * sizeof(Symbol);
    }

    std::pair<std::unordered_set<std::vector<const Pos*>, Bindings::PosHasher>::iterator, bool> res2 = Bindings::pposTable->emplace(std::move(positions));
    if (res2.second) {
        Bindings::nPosLists ++;
        Bindings::bPosLists += savedsize * sizeof(Pos*);
    }

    Bindings * b = new (p) Bindings(&*res1.first, &*res2.first);
    memcpy(&b->values[0], &values[0], savedsize * sizeof(Value *));
    return b;
}

Bindings * BindingsBuilder::result(bool alreadySorted) {
    return resultAt(allocBytes(sizeof(Bindings) + size()*sizeof(Value*)), alreadySorted);
}

// own sort function because std::sort has no custom `swap' function :(
void BindingsBuilder::quicksort(iterator a, iterator b) {
    if (2 <= b - a) {
        Symbol pivot = names.symOrder[a + (b-a)/2];
        iterator i = a, j = b - 1;
        while (true) {
            while (names.symOrder[i] < pivot) ++i;
            while (pivot < names.symOrder[j]) --j;
            assert(i <= j);
            if (i == j) break;
            std::swap(names.symOrder[i], names.symOrder[j]);
            std::swap(values[i],         values[j]        );
            std::swap(positions[i],      positions[j]     );
        };
        quicksort(a,   j);
        quicksort(i+1, b);
    }
}


void EvalState::mkAttrs(Value & v, BindingsBuilder & bb, bool alreadySorted)
{
    if (bb.size() == 0) {
        v = vEmptySet;
        return;
    }

    nrAttrsets++;
    nrAttrsInAttrsets += bb.size();

    clearValue(v);
    v.type = tAttrs;
    v.attrs = bb.result(alreadySorted);
}

}
