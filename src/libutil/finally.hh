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
    bool movedFrom = false;

public:
    Finally(Fn fun) : fun(std::move(fun)) { }
    // Copying Finallys is definitely not a good idea and will cause them to be
    // called twice.
    Finally(Finally &other) = delete;
    Finally(Finally &&other) : fun(std::move(other.fun)) {
        other.movedFrom = true;
    }
    ~Finally() { if (!movedFrom) fun(); }
};
