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
// internals.hpp :  internal structs. included by format.hpp
//                              stream_format_state, and format_item
// ----------------------------------------------------------------------------


#ifndef BOOST_FORMAT_INTERNALS_HPP
#define BOOST_FORMAT_INTERNALS_HPP


#include <string>
#include <sstream>

namespace boost {
namespace io {
namespace detail {


// --------------
// set of params that define the format state of a stream

struct stream_format_state 
{
  typedef std::ios   basic_ios;

  std::streamsize width_;
  std::streamsize precision_;
  char fill_; 
  std::ios::fmtflags flags_;

  stream_format_state()       : width_(-1), precision_(-1), fill_(0), flags_(std::ios::dec)  {}
  stream_format_state(basic_ios& os)                  {set_by_stream(os); }

  void apply_on(basic_ios & os) const;                //- applies format_state to the stream
  template<class T> void apply_manip(T manipulator)   //- modifies state by applying manipulator.
       { apply_manip_body<T>( *this, manipulator) ; }
  void reset();                                       //- sets to default state.
  void set_by_stream(const basic_ios& os);            //- sets to os's state.
};  



// --------------
// format_item : stores all parameters that can be defined by directives in the format-string

struct format_item 
{     
  enum pad_values { zeropad = 1, spacepad =2, centered=4, tabulation = 8 };

  enum arg_values { argN_no_posit   = -1, // non-positional directive. argN will be set later.
                    argN_tabulation = -2, // tabulation directive. (no argument read) 
                    argN_ignored    = -3  // ignored directive. (no argument read)
  };
  typedef BOOST_IO_STD ios              basic_ios;
  typedef detail::stream_format_state         stream_format_state;
  typedef std::string           string_t;
  typedef BOOST_IO_STD ostringstream    internal_stream_t;


  int         argN_;           //- argument number (starts at 0,  eg : %1 => argN=0)
                               //  negative values are used for items that don't process
                               //  an argument
  string_t    res_;            //- result of the formatting of this item
  string_t    appendix_;       //- piece of string between this item and the next

  stream_format_state ref_state_;// set by parsing the format_string, is only affected by modify_item
  stream_format_state state_;  // always same as ref_state, _unless_ modified by manipulators 'group(..)'

  // non-stream format-state parameters
  signed int truncate_;        //- is >=0 for directives like %.5s (take 5 chars from the string)
  unsigned int pad_scheme_;    //- several possible padding schemes can mix. see pad_values

  format_item() : argN_(argN_no_posit), truncate_(-1), pad_scheme_(0)  {}

  void compute_states();      // sets states  according to truncate and pad_scheme.
}; 



// -----------------------------------------------------------
// Definitions
// -----------------------------------------------------------

// --- stream_format_state:: -------------------------------------------
inline
void stream_format_state::apply_on(basic_ios & os) const
  // set the state of this stream according to our params
{
      if(width_ != -1)
        os.width(width_);
      if(precision_ != -1)
        os.precision(precision_);
      if(fill_ != 0)
        os.fill(fill_);
      os.flags(flags_);
}

inline
void stream_format_state::set_by_stream(const basic_ios& os) 
  // set our params according to the state of this stream
{
      flags_ = os.flags();
      width_ = os.width();
      precision_ = os.precision();
      fill_ = os.fill();
}

template<class T>  inline
void apply_manip_body( stream_format_state& self,
                       T manipulator) 
  // modify our params according to the manipulator
{
      BOOST_IO_STD stringstream  ss;
      self.apply_on( ss );
      ss << manipulator;
      self.set_by_stream( ss );
}

inline
void stream_format_state::reset() 
  // set our params to standard's default state
{
      width_=-1; precision_=-1; fill_=0; 
      flags_ = std::ios::dec; 
}


// --- format_items:: -------------------------------------------
inline
void format_item::compute_states() 
  // reflect pad_scheme_   on  state_ and ref_state_ 
  //   because some pad_schemes has complex consequences on several state params.
{
  if(pad_scheme_ & zeropad) 
  {
    if(ref_state_.flags_ & std::ios::left) 
    {
      pad_scheme_ = pad_scheme_ & (~zeropad); // ignore zeropad in left alignment
    }
    else 
    { 
      ref_state_.fill_='0'; 
      ref_state_.flags_ |= std::ios::internal;
    }
  }
  state_ = ref_state_;
}


} } } // namespaces boost :: io :: detail


#endif // BOOST_FORMAT_INTERNALS_HPP
