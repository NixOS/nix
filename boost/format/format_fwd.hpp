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

class basic_format;

typedef basic_format    format;

namespace io {
enum format_error_bits { bad_format_string_bit = 1, 
                         too_few_args_bit = 2, too_many_args_bit = 4,
                         out_of_range_bit = 8,
                         all_error_bits = 255, no_error_bits=0 };
                  
// Convertion:  format   to   string
std::string     str(const basic_format& ) ;

} // namespace io


BOOST_IO_STD ostream& 
operator<<( BOOST_IO_STD ostream&, const basic_format&);


} // namespace boost

#endif // BOOST_FORMAT_FWD_HPP
