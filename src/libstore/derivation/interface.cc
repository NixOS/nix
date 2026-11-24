#include "nix/store/derivation/interface.hh"

namespace nix::derivation {

BasicOnly<StorePath>::~BasicOnly() = default;

template<typename Input>
StringSet Interface<Input>::outputNames() const
{
    StringSet names;
    for (auto & [name, _] : outputs())
        names.insert(name);
    return names;
}

template<typename Input>
bool Interface<Input>::isBuiltin() const
{
    /* The builder of a builtin derivation is literal text, so how
       references would be written out does not matter. */
    return builder([](const auto &) { return std::string{}; }).starts_with("builtin:");
}

template<typename Input>
StringSet Interface<Input>::getRequiredSystemFeatures() const
{
    StringSet res;
    for (auto & i : options().requiredSystemFeatures)
        res.insert(i);
    /* A machine that builds a derivation whose output paths are not
       known in advance must know about content addressing. */
    for (auto & [_, output] : outputs())
        if (!std::holds_alternative<Output::InputAddressed>(output.output.raw)
            && !std::holds_alternative<Output::CAFixed>(output.output.raw)) {
            res.insert("ca-derivations");
            break;
        }
    return res;
}

template<typename Input>
bool Interface<Input>::useUidRange() const
{
    return getRequiredSystemFeatures().contains("uid-range");
}

template class Interface<SingleDerivedPath>;
template class Interface<StorePath>;

} // namespace nix::derivation
