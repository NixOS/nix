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
// free_funcs.hpp :  implementation of the free functions declared in namespace format
// ------------------------------------------------------------------------------

#ifndef BOOST_FORMAT_FUNCS_HPP
#define BOOST_FORMAT_FUNCS_HPP

#include "boost/format.hpp"
#include "boost/throw_exception.hpp"

namespace boost {

namespace io {
  inline 
  std::string str(const basic_format& f) 
    // adds up all pieces of strings and converted items, and return the formatted string
  {
    return f.str();
  }
}   // - namespace io

BOOST_IO_STD ostream& 
operator<<( BOOST_IO_STD ostream& os, 
            const boost::basic_format& f) 
  // effect: "return os << str(f);" but we can try to do it faster
{
  typedef boost::basic_format   format_t;
  if(f.items_.size()==0) 
    os << f.prefix_;
  else {
    if(f.cur_arg_ < f.num_args_)
      if( f.exceptions() & io::too_few_args_bit )
        boost::throw_exception(io::too_few_args()); // not enough variables have been supplied !
    if(f.style_ & format_t::special_needs) 
        os << f.str();
    else {
    // else we dont have to count chars output, so we dump directly to os :
      os << f.prefix_;
      for(unsigned long i=0; i<f.items_.size(); ++i) 
        {
          const format_t::format_item_t& item = f.items_[i];
          os << item.res_;
          os << item.appendix_;

        }
    }
  }
  f.dumped_=true;
  return os;
}



} // namespace boost


#endif // BOOST_FORMAT_FUNCS_HPP
