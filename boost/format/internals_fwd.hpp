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
// internals_fwd.hpp :  forward declarations, for internal headers
// ------------------------------------------------------------------------------

#ifndef BOOST_FORMAT_INTERNAL_FWD_HPP
#define BOOST_FORMAT_INTERNAL_FWD_HPP

#include "boost/format/format_fwd.hpp"


namespace boost {
namespace io {

namespace detail {
  template<class Ch, class Tr> struct stream_format_state;
  template<class Ch, class Tr> struct format_item;
}


namespace detail {

  // these functions were intended as methods, 
  // but MSVC have problems with template member functions :

  // defined in format_implementation.hpp :
     template<class Ch, class Tr, class T> 
     basic_format<Ch, Tr>&  modify_item_body( basic_format<Ch, Tr>& self, 
                                          int itemN, const T& manipulator);

     template<class Ch, class Tr, class T> 
     basic_format<Ch, Tr>&  bind_arg_body( basic_format<Ch, Tr>& self,
                                           int argN, const T& val);

    template<class Ch, class Tr, class T> 
    void apply_manip_body( stream_format_state<Ch, Tr>& self,
                           T manipulator);

  // argument feeding (defined in feed_args.hpp ) :
     template<class Ch, class Tr, class T> 
     void distribute(basic_format<Ch,Tr>& self, T x);

     template<class Ch, class Tr, class T> 
     basic_format<Ch, Tr>& feed(basic_format<Ch,Tr>& self, T x);
 
} // namespace detail

} // namespace io
} // namespace boost


#endif //  BOOST_FORMAT_INTERNAL_FWD_HPP
