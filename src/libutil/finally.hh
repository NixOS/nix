#pragma once
///@file

#include <utility>

/**
 * A trivial class to run a function at the end of a scope.
 */
template<typename Fn>
class [[nodiscard("Finally values must be used")]] Finally
{
private:
    Fn fun;

public:
    Finally(Fn fun) : fun(std::move(fun)) { }
    ~Finally() { fun(); }
};
