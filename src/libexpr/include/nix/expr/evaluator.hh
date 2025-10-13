#pragma once
/**
 * @file
 * Abstract Evaluator interface for Nix expression evaluation.
 */

#include <memory>
#include <vector>

#include "nix/store/path.hh"
#include "nix/expr/value/context.hh"
#include "nix/expr/value.hh"
#include "nix/expr/object-type.hh"
#include "nix/util/ref.hh"
#include "nix/util/source-path.hh"

namespace nix {

class Store;

namespace fetchers {
struct Settings;
}

/**
 * Representation of a Nix language value or potential value.
 * https://nix.dev/manual/nix/latest/language/evaluation.html#values
 *
 * Design note: This interface uses `std::string` for attribute names instead of `Symbol`.
 * Ideally, we could share a single SymbolTable and use `Symbol` at this level for better performance.
 * TODO: Consider adding Symbol support.
 */
class Object : public std::enable_shared_from_this<Object>
{
public:
    virtual ~Object() = default;

    /**
     * Get an attribute by name, like `a.attr or`.
     * Returns `nullptr` if not found or when this is not an attribute set.
     */
    virtual std::shared_ptr<Object> maybeGetAttr(const std::string & name) = 0;

    /**
     * Get the attribute names of this object.
     * Throws a TypeError if this is not an attribute set.
     * Returns an empty vector if this is an empty attribute set.
     * @return Vector of attribute names
     */
    virtual std::vector<std::string> getAttrNames() = 0;

    /**
     * Get the string value, ignoring any context.
     * Throws an error if not a string.
     */
    virtual std::string getStringIgnoreContext() = 0;

    /**
     * Get the string value with its context.
     * Throws an error if not a string.
     * The context is a set of store paths that the string references.
     */
    virtual std::pair<std::string, NixStringContext> getStringWithContext() = 0;

    /**
     * Get the path value.
     * Throws an error if not a path.
     * Note: Paths are not cached by EvalCache, so this always forces evaluation.
     */
    virtual SourcePath getPath() = 0;

    /**
     * Get the boolean value.
     * Throws an error if not a boolean.
     * @param errorCtx Context for error messages (optional)
     */
    virtual bool getBool(std::string_view errorCtx = "") = 0;

    /**
     * Get the integer value.
     * Throws an error if not an integer.
     * @param errorCtx Context for error messages (optional)
     */
    virtual NixInt getInt(std::string_view errorCtx = "") = 0;

    /**
     * Get a list of strings, ensuring none have context.
     * Throws an error if not a list, if any element is not a string, or if any string has context.
     * @return A vector of strings without context
     *
     * Design note: this should probably not have been a primitive, but CoarseEvalCache *does* treat it that way,
     * forcing our hand.
     */
    virtual std::vector<std::string> getListOfStringsNoCtx() = 0;

    /**
     * Get the type of this object without forcing evaluation.
     * May return nThunk if the value has not been evaluated yet.
     * @return The ObjectType of this object
     */
    virtual ObjectType getTypeLazy() = 0;

    /**
     * Get the type of this object, forcing evaluation if necessary.
     * Will never return nThunk - will force evaluation and return the actual type.
     * @return The ObjectType of this object after forcing
     */
    virtual ObjectType getType() = 0;

    /**
     * Defeat the cache and get the underlying forced Value.
     * This bypasses the lossy CoarseEvalCache (e.g., paths cached as strings without context)
     * and forces evaluation of the original expression to get the actual Value.
     * Use this when you need accurate type information or when the cache is lossy.
     * @return A RootValue containing the forced Value
     */
    virtual RootValue defeatCache() = 0;
};

/**
 * Abstract interface for Nix expression evaluation.
 */
class Evaluator
{
public:
    virtual ~Evaluator() = default;

    /**
     * Check if the evaluator is in read-only mode.
     * In read-only mode, operations that would modify the store are disallowed.
     */
    virtual bool isReadOnly() const = 0;

    /**
     * Get the store associated with this evaluator.
     */
    virtual Store & getStore() = 0;

    /**
     * Get the fetch settings for this evaluator.
     */
    virtual const fetchers::Settings & getFetchSettings() = 0;
};

} // namespace nix
