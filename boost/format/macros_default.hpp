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
// macros_default.hpp : configuration for the format library
//                       provides default values for the stl workaround macros
// ------------------------------------------------------------------------------

#ifndef BOOST_FORMAT_MACROS_DEFAULT_HPP
#define BOOST_FORMAT_MACROS_DEFAULT_HPP

// *** This should go to "boost/config/suffix.hpp".

#ifndef BOOST_IO_STD
#  define BOOST_IO_STD std::
#endif

// **** Workaround for io streams, stlport and msvc.
#ifdef BOOST_IO_NEEDS_USING_DECLARATION
namespace boost {
  using std::char_traits;
  using std::basic_ostream;
  using std::basic_ostringstream;
  namespace io {
    using std::basic_ostream;
    namespace detail {
      using std::basic_ios;
      using std::basic_ostream;
      using std::basic_ostringstream;
    }
  }
}
#endif

// ------------------------------------------------------------------------------

#endif // BOOST_FORMAT_MACROS_DEFAULT_HPP
