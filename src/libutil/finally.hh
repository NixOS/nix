#pragma once

#include <functional>

/* A trivial class to run a function at the end of a scope. */
class Finally
{
private:
    std::function<void()> fun;

public:
    Finally(std::function<void()> fun) : fun(fun) { }
    ~Finally() { fun(); }
};
