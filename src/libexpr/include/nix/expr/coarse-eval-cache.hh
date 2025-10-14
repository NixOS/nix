#pragma once
/**
 * @file
 * CoarseEvalCache - Evaluator implementation using the coarse-grained eval cache.
 */

#include "nix/expr/evaluator.hh"
#include "nix/util/ref.hh"

namespace nix {

class EvalState;

namespace eval_cache {
class EvalCache;
}

/**
 * Evaluator implementation that uses a coarse-grained cache key to remember
 * a Nix value.
 *
 * `CoarseEvalCache` is typically used with a flake lock as its cache key, and
 * the flake outputs of the root flake as the cached value.
 *
 * EvalState manages the cache of EvalCaches internally, so this class
 * just coordinates between the caller and the cached evaluation.
 */
class CoarseEvalCache : public Evaluator
{
    ref<EvalState> state;

public:
    explicit CoarseEvalCache(ref<EvalState> state);

    /**
     * Get the root Object from an EvalCache.
     * This creates an Object interface for navigating the evaluation results,
     * which may trigger evaluation if the values aren't already cached.
     */
    ref<Object> getRoot(ref<eval_cache::EvalCache> evalCache);

    bool isReadOnly() const override;

    Store & getStore() override;

    const fetchers::Settings & getFetchSettings() override;
};

} // namespace nix