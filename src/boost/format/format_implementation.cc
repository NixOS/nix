// -*- C++ -*-
//  Boost general library format ---------------------------
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
// format_implementation.hpp  Implementation of the basic_format class
// ----------------------------------------------------------------------------


#ifndef BOOST_FORMAT_IMPLEMENTATION_HPP
#define BOOST_FORMAT_IMPLEMENTATION_HPP

//#include <boost/throw_exception.hpp>
//#include <boost/assert.hpp>
#include <boost/format.hpp>

namespace boost {

// --------  format:: -------------------------------------------
basic_format::basic_format(const char* str)
    : style_(0), cur_arg_(0), num_args_(0), dumped_(false),
      items_(), oss_(), exceptions_(io::all_error_bits)
{
    state0_.set_by_stream(oss_);
    string_t emptyStr;
    if( !str) str = emptyStr.c_str();
    parse( str );
}

#ifndef BOOST_NO_STD_LOCALE
basic_format::basic_format(const char* str, const std::locale & loc)
    : style_(0), cur_arg_(0), num_args_(0), dumped_(false),
      items_(), oss_(), exceptions_(io::all_error_bits)
{
    oss_.imbue( loc );
    state0_.set_by_stream(oss_);
    string_t emptyStr;
    if( !str) str = emptyStr.c_str();
    parse( str );
}

basic_format::basic_format(const string_t& s, const std::locale & loc)
    : style_(0), cur_arg_(0), num_args_(0), dumped_(false),
      items_(),  oss_(), exceptions_(io::all_error_bits)
{
    oss_.imbue( loc );
    state0_.set_by_stream(oss_);
    parse(s);  
}
#endif //BOOST_NO_STD_LOCALE

basic_format::basic_format(const string_t& s)
    : style_(0), cur_arg_(0), num_args_(0), dumped_(false),
      items_(),  oss_(), exceptions_(io::all_error_bits)
{
    state0_.set_by_stream(oss_);
    parse(s);  
}

basic_format:: basic_format(const basic_format& x)
    : style_(x.style_), cur_arg_(x.cur_arg_), num_args_(x.num_args_), dumped_(false), 
      items_(x.items_), prefix_(x.prefix_), bound_(x.bound_), 
      oss_(),   // <- we obviously can't copy x.oss_
      state0_(x.state0_), exceptions_(x.exceptions_)
{ 
    state0_.apply_on(oss_);
} 

basic_format& basic_format::operator= (const basic_format& x)
{
    if(this == &x)
      return *this;
    state0_ = x.state0_;
    state0_.apply_on(oss_);

    // plus all the other (trivial) assignments :
    exceptions_ = x.exceptions_;
    items_ = x.items_;
    prefix_ = x.prefix_;
    bound_=x.bound_;
    style_=x.style_; 
    cur_arg_=x.cur_arg_; 
    num_args_=x.num_args_;
    dumped_=x.dumped_;
    return *this;
}


unsigned char basic_format::exceptions() const 
{
  return exceptions_; 
}

unsigned char basic_format::exceptions(unsigned char newexcept) 
{ 
  unsigned char swp = exceptions_; 
  exceptions_ = newexcept; 
  return swp; 
}


basic_format& basic_format ::clear()
  // empty the string buffers (except bound arguments, see clear_binds() )
  // and make the format object ready for formatting a new set of arguments
{
    BOOST_ASSERT( bound_.size()==0 || num_args_ == static_cast<int>(bound_.size()) );

    for(unsigned long i=0; i<items_.size(); ++i){
      items_[i].state_ = items_[i].ref_state_;
      // clear converted strings only if the corresponding argument is not  bound :
      if( bound_.size()==0 || !bound_[ items_[i].argN_ ] )  items_[i].res_.resize(0);
    }
    cur_arg_=0; dumped_=false;
    // maybe first arg is bound:
    if(bound_.size() != 0)
      {
        while(cur_arg_ < num_args_ && bound_[cur_arg_] )      ++cur_arg_;
      }
    return *this;
}

basic_format& basic_format ::clear_binds() 
  // cancel all bindings, and clear()
{
    bound_.resize(0);
    clear();
    return *this;
}

basic_format& basic_format::clear_bind(int argN) 
  // cancel the binding of ONE argument, and clear()
{
    if(argN<1 || argN > num_args_ || bound_.size()==0 || !bound_[argN-1] ) 
      {
        if( exceptions() & io::out_of_range_bit )
          boost::throw_exception(io::out_of_range()); // arg not in range.
        else return *this;
      }
    bound_[argN-1]=false;
    clear();
    return *this;
}



std::string basic_format::str() const
{
  dumped_=true;
  if(items_.size()==0)
    return prefix_;
  if( cur_arg_ < num_args_)
      if( exceptions() & io::too_few_args_bit )
        boost::throw_exception(io::too_few_args()); // not enough variables have been supplied !

  unsigned long sz = prefix_.size();
  unsigned long i;
  for(i=0; i < items_.size(); ++i) 
    sz += items_[i].res_.size() + items_[i].appendix_.size();
  string_t res;
  res.reserve(sz);

  res += prefix_;
  for(i=0; i < items_.size(); ++i) 
  {
    const format_item_t& item = items_[i];
    res += item.res_;
    if( item.argN_ == format_item_t::argN_tabulation) 
    { 
      BOOST_ASSERT( item.pad_scheme_ & format_item_t::tabulation);
      std::streamsize  n = item.state_.width_ - res.size();
      if( n > 0 )
        res.append( n, item.state_.fill_ );
    }
    res += item.appendix_;
  }
  return res;
}

namespace io {
namespace detail {

template<class T>
basic_format&  bind_arg_body( basic_format& self, 
                                      int argN, 
                                      const T& val)
  // bind one argument to a fixed value
  // this is persistent over clear() calls, thus also over str() and <<
{
    if(self.dumped_) self.clear(); // needed, because we will modify cur_arg_..
    if(argN<1 || argN > self.num_args_) 
      {
        if( self.exceptions() & io::out_of_range_bit )
          boost::throw_exception(io::out_of_range()); // arg not in range.
        else return self;
      }
    if(self.bound_.size()==0) 
      self.bound_.assign(self.num_args_,false);
    else 
      BOOST_ASSERT( self.num_args_ == static_cast<signed int>(self.bound_.size()) );
    int o_cur_arg = self.cur_arg_;
    self.cur_arg_ = argN-1; // arrays begin at 0

    self.bound_[self.cur_arg_]=false; // if already set, we unset and re-sets..
    self.operator%(val); // put val at the right place, because cur_arg is set
    

    // Now re-position cur_arg before leaving :
    self.cur_arg_ = o_cur_arg; 
    self.bound_[argN-1]=true;
    if(self.cur_arg_ == argN-1 )
      // hum, now this arg is bound, so move to next free arg
      {
        while(self.cur_arg_ < self.num_args_ && self.bound_[self.cur_arg_])   ++self.cur_arg_;
      }
    // In any case, we either have all args, or are on a non-binded arg :
    BOOST_ASSERT( self.cur_arg_ >= self.num_args_ || ! self.bound_[self.cur_arg_]);
    return self;
}

template<class T>
basic_format&  modify_item_body( basic_format& self,
                                      int itemN, 
                                      const T& manipulator)
  // applies a manipulator to the format_item describing a given directive.
  // this is a permanent change, clear or clear_binds won't cancel that.
{
  if(itemN<1 || itemN >= static_cast<signed int>(self.items_.size() )) 
    {
      if( self.exceptions() & io::out_of_range_bit ) 
        boost::throw_exception(io::out_of_range()); // item not in range.
      else return self;
    }
  self.items_[itemN-1].ref_state_.apply_manip( manipulator );
  self.items_[itemN-1].state_ = self.items_[itemN-1].ref_state_;
  return self;
}

} // namespace detail

} // namespace io

} // namespace boost



#endif  // BOOST_FORMAT_IMPLEMENTATION_HPP
