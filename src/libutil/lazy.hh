#include <exception>
#include <functional>
#include <mutex>

namespace nix {

/* A helper class for lazily-initialized variables.

     Lazy<T> var([]() { return value; });

   declares a variable of type T that is initialized to 'value' (in a
   thread-safe way) on first use, that is, when var() is first
   called. If the initialiser code throws an exception, then all
   subsequent calls to var() will rethrow that exception. */
template<typename T>
class Lazy
{

    typedef std::function<T()> Init;

    Init init;

    std::once_flag done;

    T value;

    std::exception_ptr ex;

public:

    Lazy(Init init) : init(init)
    { }

    const T & operator () ()
    {
        std::call_once(done, [&]() {
            try {
                value = init();
            } catch (...) {
                ex = std::current_exception();
            }
        });
        if (ex) std::rethrow_exception(ex);
        return value;
    }
};

}
