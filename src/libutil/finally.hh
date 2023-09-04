#pragma once

/* A trivial class to run a function at the end of a scope. */
template<typename Fn>
class Finally
{
private:
    Fn fun;

public:
    Finally(Fn fun) : fun(std::move(fun)) { }
    ~Finally() { fun(); }
};
