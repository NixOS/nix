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
// format_fwd.hpp :  forward declarations, for primary header format.hpp
// ------------------------------------------------------------------------------

#ifndef BOOST_FORMAT_FWD_HPP
#define BOOST_FORMAT_FWD_HPP

#include <string>
#include <iosfwd>

namespace boost {

template<class charT, class Traits = BOOST_IO_STD char_traits<charT> > class basic_format;

typedef basic_format<char >     format;

#if !defined(BOOST_NO_STD_WSTRING) && !defined(BOOST_NO_STD_WSTREAMBUF)
typedef basic_format<wchar_t >  wformat;
#endif

namespace io {
enum format_error_bits { bad_format_string_bit = 1, 
                         too_few_args_bit = 2, too_many_args_bit = 4,
                         out_of_range_bit = 8,
                         all_error_bits = 255, no_error_bits=0 };
                  
// Convertion:  format   to   string
template<class Ch, class Tr> 
std::basic_string<Ch, Tr>     str(const basic_format<Ch, Tr>& ) ;

} // namespace io


template< class Ch, class Tr> 
BOOST_IO_STD basic_ostream<Ch, Tr>& 
operator<<( BOOST_IO_STD basic_ostream<Ch, Tr>&, const basic_format<Ch, Tr>&);


} // namespace boost

#endif // BOOST_FORMAT_FWD_HPP
