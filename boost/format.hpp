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
// and Karl Nelson's ofstream

// ----------------------------------------------------------------------------
// format.hpp :  primary header
// ----------------------------------------------------------------------------

#ifndef BOOST_FORMAT_HPP
#define BOOST_FORMAT_HPP

#include <vector>
#include <string>
#include <sstream>
#include <cassert>

#include <locale>

#include <boost/format/macros_default.hpp>

namespace boost 
{
    template<class E> void throw_exception(E const & e)
    {
        throw e;
    }
}

#define BOOST_ASSERT(expr) assert(expr)


// ****  Forward declarations ----------------------------------
#include <boost/format/format_fwd.hpp>           // basic_format<Ch,Tr>, and other frontends
#include <boost/format/internals_fwd.hpp>        // misc forward declarations for internal use


// ****  Auxiliary structs (stream_format_state<Ch,Tr> , and format_item<Ch,Tr> )
#include <boost/format/internals.hpp>    

// ****  Format  class  interface --------------------------------
#include <boost/format/format_class.hpp>

// **** Exceptions -----------------------------------------------
#include <boost/format/exceptions.hpp>

// **** Implementation -------------------------------------------
#include <boost/format/format_implementation.hpp>   // member functions

#include <boost/format/group.hpp>                   // class for grouping arguments

#include <boost/format/feed_args.hpp>               // argument-feeding functions
#include <boost/format/parsing.hpp>                 // format-string parsing (member-)functions

// **** Implementation of the free functions ----------------------
#include <boost/format/free_funcs.hpp>


#endif // BOOST_FORMAT_HPP
