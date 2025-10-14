#include "nix/expr/coarse-eval-cache-cursor-object.hh"
#include "nix/expr/eval.hh"

namespace nix {

CoarseEvalCacheCursorObject::CoarseEvalCacheCursorObject(ref<eval_cache::AttrCursor> cursor)
    : cursor(cursor)
{
}

std::shared_ptr<Object> CoarseEvalCacheCursorObject::maybeGetAttr(const std::string & name)
{
    auto attr = cursor->maybeGetAttr(name);
    if (!attr)
        return nullptr;
    return std::make_shared<CoarseEvalCacheCursorObject>(ref(attr));
}

std::vector<std::string> CoarseEvalCacheCursorObject::getAttrNames()
{
    // getAttrs() already throws if not an attrset
    auto attrs = cursor->getAttrs();
    std::vector<std::string> result;
    for (auto & attr : attrs) {
        result.push_back(std::string(cursor->root->state.symbols[attr]));
    }
    return result;
}

std::string CoarseEvalCacheCursorObject::getStringIgnoreContext()
{
    // Use getString() which uses the cache and throws if not a string
    return cursor->getString();
}

std::pair<std::string, NixStringContext> CoarseEvalCacheCursorObject::getStringWithContext()
{
    return cursor->getStringWithContext();
}

SourcePath CoarseEvalCacheCursorObject::getPath()
{
    // Paths are not cached by EvalCache, so we need to force evaluation
    // But first check the lazy type to avoid forcing if it's definitely not a path
    auto lazyType = getTypeLazy();
    if (lazyType != nThunk && lazyType != nPath) {
        // We know it's not a path and not a thunk, so error without forcing
        cursor->root->state.error<TypeError>("expected a path but found %1%", showType(lazyType)).debugThrow();
    }

    // Either it's a thunk (need to force to find out) or it's a path (need to force to get value)
    auto & v = cursor->forceValue();
    if (v.type() != nPath)
        cursor->root->state.error<TypeError>("expected a path but found %1%", showType(v)).debugThrow();
    return v.path();
}

bool CoarseEvalCacheCursorObject::getBool(std::string_view errorCtx)
{
    try {
        return cursor->getBool();
    } catch (Error & e) {
        if (!errorCtx.empty())
            e.addTrace(nullptr, errorCtx);
        throw;
    }
}

NixInt CoarseEvalCacheCursorObject::getInt(std::string_view errorCtx)
{
    try {
        return cursor->getInt();
    } catch (Error & e) {
        if (!errorCtx.empty())
            e.addTrace(nullptr, errorCtx);
        throw;
    }
}

std::vector<std::string> CoarseEvalCacheCursorObject::getListOfStringsNoCtx()
{
    return cursor->getListOfStrings();
}

ObjectType CoarseEvalCacheCursorObject::getTypeLazy()
{
    return cursor->getTypeLazy();
}

ObjectType CoarseEvalCacheCursorObject::getType()
{
    auto type = cursor->getTypeLazy();
    if (type != nThunk)
        return type;
    // Need to force to determine type
    return cursor->forceValue().type();
}

RootValue CoarseEvalCacheCursorObject::defeatCache()
{
    // Force evaluation and return the actual Value, bypassing the lossy cache
    return allocRootValue(&cursor->forceValue());
}

} // namespace nix