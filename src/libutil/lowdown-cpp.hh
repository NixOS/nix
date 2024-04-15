#pragma once
///@file

#include "types.hh"

#include <sys/queue.h>
#include <lowdown.h>

namespace nix::lowdown {

using Doc = struct lowdown_doc;
using Node = struct lowdown_node;
using Buf = struct lowdown_buf;

/**
 * For type-directed destructor, avoid `void *`
 *
 * @todo upstream as `lowdown_term`.
 */
struct Term;

struct LowdownDeleter
{
    void operator () (Doc * ptr)
    {
        lowdown_doc_free(ptr);
    }
    void operator () (Node * ptr)
    {
        lowdown_node_free(ptr);
    }
    void operator () (Term * ptr)
    {
        lowdown_term_free(ptr);
    }
    void operator () (Buf * ptr)
    {
        lowdown_buf_free(ptr);
    }
};

template <typename T>
using UniquePtr = std::unique_ptr<T, LowdownDeleter>;

}
