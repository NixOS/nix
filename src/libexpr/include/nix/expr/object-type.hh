#pragma once

namespace nix {

/**
 * A thunk, value or external
 *
 * `ObjectType` is a broad categorization of lazy value representations. This includes
 *   - Thunk: the possibility that a value has not been evaluated yet or, in some cases, may not be possible to evaluate
 * to a value.
 *   - "External" values defined by plugins or applications that extend the Nix language.
 *   - Values as laid out in https://nix.dev/manual/nix/latest/language/types.html,
 *     and distinguished by `builtins.typeOf`.
 *     This typically corresponds to a value in at least
 * [WHNF](https://nix.dev/manual/nix/latest/language/evaluation.html#values).
 *
 * In the `Interpreter` this is/was called `ValueType`, but that is a slight
 * misnomer, because a thunk that fails may have been _intended_ as a value,
 * but does not _represent_ a value.
 */
typedef enum {
    nThunk,
    nInt,
    nFloat,
    nBool,
    nString,
    nPath,
    nNull,
    nAttrs,
    nList,
    nFunction,
    nExternal,
} ObjectType;

} // namespace nix
