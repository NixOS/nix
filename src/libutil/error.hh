#pragma once


#include "ref.hh"

#include <list>
#include <memory>
#include <map>
#include <optional>

#include "fmt.hh"

/* Before 4.7, gcc's std::exception uses empty throw() specifiers for
 * its (virtual) destructor and what() in c++11 mode, in violation of spec
 */
#ifdef __GNUC__
#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#define EXCEPTION_NEEDS_THROW_SPEC
#endif
#endif

namespace nix {

using std::list;
using std::vector;

typedef enum {
    lvlError = 0,
    lvlWarn,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

struct ErrPos
{
    int line;
    int column;
    string file;

    template <class P>
    ErrPos& operator=(const P &pos)
    {
        line = pos.line;
        column = pos.column;
        file = pos.file;
        return *this;
    }

    template <class P>
    ErrPos(const P &p)
    {
      *this = p;
    }
};

struct NixCode
{
    ErrPos errPos;
    std::optional<string> prevLineOfCode;
    string errLineOfCode;
    std::optional<string> nextLineOfCode;
};

// -------------------------------------------------
// ErrorInfo.
struct ErrorInfo
{
    Verbosity level;
    string name;
    string description;
    std::optional<hintformat> hint;
    std::optional<NixCode> nixCode;

    static std::optional<string> programName;
};

std::ostream& operator<<(std::ostream &out, const ErrorInfo &einfo);

/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception
{
protected:
    string prefix_; // used for location traces etc.
    ErrorInfo err;
    string what_;
    void initWhat() 
    {
        std::ostringstream oss;
        oss << err;
        what_ = oss.str();
    }
public:
    unsigned int status = 1; // exit status

    template<typename... Args>
    BaseError(unsigned int status, const Args & ... args)
        : err { .level = lvlError,
                .hint = hintfmt(args...)
              }
        , status(status)
    { initWhat(); }

    template<typename... Args>
    BaseError(const Args & ... args)
        : err { .level = lvlError, 
                .hint = hintfmt(args...)
              }
    { initWhat(); }

    BaseError(ErrorInfo e)
        : err(e)
    { initWhat(); }

#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~BaseError() throw () { };
    const char * what() const throw () { return what_.c_str(); }
#else
    const char * what() const noexcept { return what_.c_str(); }
#endif

    const string & msg() const { return what_; }
    const string & prefix() const { return prefix_; }
    BaseError & addPrefix(const FormatOrString & fs);

    const ErrorInfo & info() const { return err; }
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);

class SysError : public Error
{
public:
    int errNo;

    template<typename... Args>
    SysError(const Args & ... args)
        : Error(args...)  // TODO addErrNo for hintfmt
        // : Error(addErrno(hintfmt(args...)))
    { }

private:

    std::string addErrno(const std::string & s);
};

}
