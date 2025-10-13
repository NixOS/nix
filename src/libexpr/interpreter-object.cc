#include "nix/expr/interpreter-object.hh"

namespace nix {

InterpreterObject::InterpreterObject(EvalState & state, RootValue value)
    : state(state)
    , value(value)
{
}

std::shared_ptr<Object> InterpreterObject::maybeGetAttr(const std::string & name)
{
    state.forceValue(**value, noPos);
    if ((*value)->type() != nAttrs)
        return nullptr;
    auto attr = (*value)->attrs()->get(state.symbols.create(name));
    if (!attr)
        return nullptr;
    return std::make_shared<InterpreterObject>(state, allocRootValue(attr->value));
}

std::vector<std::string> InterpreterObject::getAttrNames()
{
    state.forceValue(**value, noPos);
    if ((*value)->type() != nAttrs)
        state.error<TypeError>("expected an attribute set but found %s", showType(**value)).debugThrow();

    std::vector<std::string> result;
    for (auto & attr : *(*value)->attrs()) {
        result.push_back(std::string(state.symbols[attr.name]));
    }
    return result;
}

std::string InterpreterObject::getStringIgnoreContext()
{
    state.forceValue(**value, noPos);
    if ((*value)->type() != nString)
        state.error<TypeError>("value is %1% while a string was expected", showType(**value)).debugThrow();
    return (*value)->c_str();
}

std::pair<std::string, NixStringContext> InterpreterObject::getStringWithContext()
{
    state.forceValue(**value, noPos);
    if ((*value)->type() != nString)
        state.error<TypeError>("value is %1% while a string was expected", showType(**value)).debugThrow();

    // Get the string value
    std::string str = (*value)->c_str();

    // Get the context using the existing copyContext function
    NixStringContext context;
    copyContext(**value, context);

    return std::make_pair(str, context);
}

SourcePath InterpreterObject::getPath()
{
    state.forceValue(**value, noPos);
    if ((*value)->type() != nPath)
        state.error<TypeError>("expected a path but found %1%", showType(**value)).debugThrow();
    return (*value)->path();
}

bool InterpreterObject::getBool(std::string_view errorCtx)
{
    // Avoid adding empty trace when errorCtx is not provided
    if (errorCtx.empty()) {
        state.forceValue(**value, noPos);
        if ((*value)->type() != nBool)
            state.error<TypeError>("expected a Boolean but found %1%", showType(**value)).debugThrow();
        return (*value)->boolean();
    }
    return state.forceBool(**value, noPos, errorCtx);
}

NixInt InterpreterObject::getInt(std::string_view errorCtx)
{
    // Avoid adding empty trace when errorCtx is not provided
    if (errorCtx.empty()) {
        state.forceValue(**value, noPos);
        if ((*value)->type() != nInt)
            state.error<TypeError>("expected an integer but found %1%", showType(**value)).debugThrow();
        return (*value)->integer();
    }
    return state.forceInt(**value, noPos, errorCtx);
}

std::vector<std::string> InterpreterObject::getListOfStringsNoCtx()
{
    state.forceValue(**value, noPos);
    if (!(*value)->isList())
        state.error<TypeError>("expected a list but found %s", showType(**value)).debugThrow();

    std::vector<std::string> result;
    size_t index = 0;
    for (auto elem : (*value)->listView()) {
        result.push_back(
            std::string(
                state.forceStringNoCtx(*elem, noPos, fmt("while evaluating a list element at index %d", index))));
        index++;
    }
    return result;
}

ObjectType InterpreterObject::getTypeLazy()
{
    return (*value)->type();
}

ObjectType InterpreterObject::getType()
{
    state.forceValue(**value, noPos);
    return (*value)->type();
}

RootValue InterpreterObject::defeatCache()
{
    // For InterpreterObject, we already have the Value, just return it
    return value;
}

} // namespace nix