// -*- C++ -*-
//  Boost general library 'format'   ---------------------------
//  See http://www.boost.org for updates, documentation, and revision history.

//  (C) Samuel Krempp 2001
//                  krempp@crans.ens-cachan.fr
//  Permission to copy, use, modify, sell and
//  distribute this software is granted provided this copyright notice appears
//  in all copies. This software is provided "as is" without express or implied
//  warranty, and with no claim as to its suitability for any purpose.

// ideas taken from Rüdiger Loos's format class
// and Karl Nelson's ofstream (also took its parsing code as basis for printf parsing)

// ------------------------------------------------------------------------------
// exceptions.hpp 
// ------------------------------------------------------------------------------


#ifndef BOOST_FORMAT_EXCEPTIONS_HPP
#define BOOST_FORMAT_EXCEPTIONS_HPP


#include <stdexcept>


namespace boost {

namespace io {

// **** exceptions -----------------------------------------------

class format_error : public std::exception
{
public:
  format_error() {}
  virtual const char *what() const throw()
  {
    return "boost::format_error: "
      "format generic failure";
  }
};

class bad_format_string : public format_error
{
public:
  bad_format_string() {}
  virtual const char *what() const throw()
  {
    return "boost::bad_format_string: "
      "format-string is ill-formed";
  }
};

class too_few_args : public format_error
{
public:
  too_few_args() {}
  virtual const char *what() const throw()
  {
    return "boost::too_few_args: "
      "format-string refered to more arguments than were passed";
  }
};

class too_many_args : public format_error
{
public:
  too_many_args() {}
  virtual const char *what() const throw()
  {
    return "boost::too_many_args: "
      "format-string refered to less arguments than were passed";
  }
};


class  out_of_range : public format_error
{
public:
  out_of_range() {}
  virtual const char *what() const throw()
  {
    return "boost::out_of_range: "
      "tried to refer to an argument (or item) number which is out of range, "
      "according to the format string.";
  }
};


} // namespace io

} // namespace boost


#endif // BOOST_FORMAT_EXCEPTIONS_HPP
