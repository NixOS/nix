#pragma once
/**
 * @file
 * Object wrapper for AttrCursor.
 */

#include "nix/expr/evaluator.hh"
#include "nix/expr/eval-cache.hh"

namespace nix {

/**
 * Object implementation that wraps an AttrCursor.
 */
class CoarseEvalCacheCursorObject : public Object
{
    ref<eval_cache::AttrCursor> cursor;

public:
    explicit CoarseEvalCacheCursorObject(ref<eval_cache::AttrCursor> cursor);

    std::shared_ptr<Object> maybeGetAttr(const std::string & name) override;

    std::vector<std::string> getAttrNames() override;

    std::string getStringIgnoreContext() override;

    std::pair<std::string, NixStringContext> getStringWithContext() override;

    SourcePath getPath() override;

    bool getBool(std::string_view errorCtx) override;

    NixInt getInt(std::string_view errorCtx) override;

    std::vector<std::string> getListOfStringsNoCtx() override;

    ObjectType getTypeLazy() override;

    ObjectType getType() override;

    RootValue defeatCache() override;
};

} // namespace nix