//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_EXCEPTION_HPP
#define TOML11_EXCEPTION_HPP

#include <array>
#include <string>
#include <stdexcept>

#include <cstring>

#include "source_location.hpp"

namespace toml
{

struct file_io_error : public std::runtime_error
{
  public:
    file_io_error(int errnum, const std::string& msg, const std::string& fname)
        : std::runtime_error(msg + " \"" + fname + "\": errno = " + std::to_string(errnum)),
          errno_(errnum)
    {}

    int get_errno() const noexcept {return errno_;}

  private:
    int errno_;
};

struct exception : public std::exception
{
  public:
    explicit exception(const source_location& loc): loc_(loc) {}
    virtual ~exception() noexcept override = default;
    virtual const char* what() const noexcept override {return "";}
    virtual source_location const& location() const noexcept {return loc_;}

  protected:
    source_location loc_;
};

struct syntax_error : public toml::exception
{
  public:
    explicit syntax_error(const std::string& what_arg, const source_location& loc)
        : exception(loc), what_(what_arg)
    {}
    virtual ~syntax_error() noexcept override = default;
    virtual const char* what() const noexcept override {return what_.c_str();}

  protected:
    std::string what_;
};

struct type_error : public toml::exception
{
  public:
    explicit type_error(const std::string& what_arg, const source_location& loc)
        : exception(loc), what_(what_arg)
    {}
    virtual ~type_error() noexcept override = default;
    virtual const char* what() const noexcept override {return what_.c_str();}

  protected:
    std::string what_;
};

struct internal_error : public toml::exception
{
  public:
    explicit internal_error(const std::string& what_arg, const source_location& loc)
        : exception(loc), what_(what_arg)
    {}
    virtual ~internal_error() noexcept override = default;
    virtual const char* what() const noexcept override {return what_.c_str();}

  protected:
    std::string what_;
};

} // toml
#endif // TOML_EXCEPTION
