#include "suggestions.hh"
#include "error.hh"

namespace nix {

// Either a value of type `T`, or some suggestions
template<typename T>
class OrSuggestions {
public:
    using Raw = std::variant<T, Suggestions>;

    Raw raw;

    T* operator ->()
    {
        return &**this;
    }

    T& operator *()
    {
        if (auto elt = std::get_if<T>(&raw))
            return *elt;
        throw Error("Invalid access to a failed value");
    }

    operator bool() const noexcept
    {
        return std::holds_alternative<T>(raw);
    }

    OrSuggestions(T t)
        : raw(t)
    {
    }

    OrSuggestions()
        : raw(Suggestions{})
    {
    }

    static OrSuggestions<T> failed(const Suggestions & s)
    {
        auto res = OrSuggestions<T>();
        res.raw = s;
        return res;
    }

    static OrSuggestions<T> failed()
    {
        return OrSuggestions<T>::failed(Suggestions{});
    }

    const Suggestions & getSuggestions()
    {
        static Suggestions noSuggestions;
        if (const auto & suggestions = std::get_if<Suggestions>(&raw))
            return *suggestions;
        else
            return noSuggestions;
    }

};

}
