#pragma once
///@file

#include "eval.hh"

#include <tuple>
#include <vector>

namespace nix {

/**
 * For functions where we do not expect deep recursion, we can use a sizable
 * part of the stack a free allocation space.
 *
 * Note: this is expected to be multiplied by sizeof(Value), or about 24 bytes.
 */
constexpr size_t nonRecursiveStackReservation = 128;

/**
 * Functions that maybe applied to self-similar inputs, such as concatMap on a
 * tree, should reserve a smaller part of the stack for allocation.
 *
 * Note: this is expected to be multiplied by sizeof(Value), or about 24 bytes.
 */
constexpr size_t conservativeStackReservation = 16;

struct RegisterPrimOp
{
    typedef std::vector<PrimOp> PrimOps;
    static PrimOps * primOps;

    /**
     * You can register a constant by passing an arity of 0. fun
     * will get called during EvalState initialization, so there
     * may be primops not yet added and builtins is not yet sorted.
     */
    RegisterPrimOp(PrimOp && primOp);
};

/* These primops are disabled without enableNativeCode, but plugins
   may wish to use them in limited contexts without globally enabling
   them. */

/**
 * Load a ValueInitializer from a DSO and return whatever it initializes
 */
void prim_importNative(EvalState & state, const PosIdx pos, Value * * args, Value & v);

/**
 * Execute a program and parse its output
 */
void prim_exec(EvalState & state, const PosIdx pos, Value * * args, Value & v);

void makePositionThunks(EvalState & state, const PosIdx pos, Value & line, Value & column);

}
