// -*- C++ -*-
//  Boost general library 'format'   ---------------------------
//  See http://www.boost.org for updates, documentation, and revision history.

//  (C) Samuel Krempp 2001
//                  krempp@crans.ens-cachan.fr
//  Permission to copy, use, modify, sell and
//  distribute this software is granted provided this copyright notice appears
//  in all copies. This software is provided "as is" without express or implied
//  warranty, and with no claim as to its suitability for any purpose.

// ideas taken from Rudiger Loos's format class
// and Karl Nelson's ofstream (also took its parsing code as basis for printf parsing)

// ------------------------------------------------------------------------------
// parsing.hpp :  implementation of the parsing member functions
//                      ( parse, parse_printf_directive)
// ------------------------------------------------------------------------------


#ifndef BOOST_FORMAT_PARSING_HPP
#define BOOST_FORMAT_PARSING_HPP


#include <boost/format.hpp>
//#include <boost/throw_exception.hpp>
//#include <boost/assert.hpp>


namespace boost {
namespace io {
namespace detail {

  template<class Stream> inline
  bool wrap_isdigit(char c, Stream &os) 
  {
#ifndef BOOST_NO_LOCALE_ISIDIGIT
    return std::isdigit(c, os.rdbuf()->getloc() );
# else
    using namespace std;
    return isdigit(c); 
#endif 
  } //end- wrap_isdigit(..)

  template<class Res> inline
  Res str2int(const std::string& s, 
              std::string::size_type start, 
              BOOST_IO_STD ios &os,
              const Res = Res(0)  ) 
    // Input : char string, with starting index
    //         a basic_ios& merely to call its widen/narrow member function in the desired locale.
    // Effects : reads s[start:] and converts digits into an integral n, of type Res
    // Returns : n
  {
    Res n = 0;
    while(start<s.size() && wrap_isdigit(s[start], os) ) {
      char cur_ch = s[start];
      BOOST_ASSERT(cur_ch != 0 ); // since we called isdigit, this should not happen.
      n *= 10;
      n += cur_ch - '0'; // 22.2.1.1.2 of the C++ standard
      ++start;
    }
    return n;
  }

  void skip_asterisk(const std::string & buf, 
                     std::string::size_type * pos_p,
                     BOOST_IO_STD ios &os)
    // skip printf's "asterisk-fields" directives in the format-string buf
    // Input : char string, with starting index *pos_p
    //         a basic_ios& merely to call its widen/narrow member function in the desired locale.
    // Effects : advance *pos_p by skipping printf's asterisk fields.
    // Returns : nothing
  {
    using namespace std;
    BOOST_ASSERT( pos_p != 0);
    if(*pos_p >= buf.size() ) return;
    if(buf[ *pos_p]=='*') {
      ++ (*pos_p);
      while (*pos_p < buf.size() && wrap_isdigit(buf[*pos_p],os)) ++(*pos_p);
      if(buf[*pos_p]=='$') ++(*pos_p);
    }
  }


  inline void maybe_throw_exception( unsigned char exceptions)
    // auxiliary func called by parse_printf_directive
    // for centralising error handling
    // it either throws if user sets the corresponding flag, or does nothing.
  {
    if(exceptions & io::bad_format_string_bit)
          boost::throw_exception(io::bad_format_string());
  }
    


  bool parse_printf_directive(const std::string & buf,
                              std::string::size_type * pos_p,
                              detail::format_item * fpar,
                              BOOST_IO_STD ios &os,
                              unsigned char exceptions)
    // Input   : a 'printf-directive' in the format-string, starting at buf[ *pos_p ]
    //           a basic_ios& merely to call its widen/narrow member function in the desired locale.
    //           a bitset'excpetions' telling whether to throw exceptions on errors.
    // Returns : true if parse somehow succeeded (possibly ignoring errors if exceptions disabled) 
    //           false if it failed so bad that the directive should be printed verbatim
    // Effects : - *pos_p is incremented so that buf[*pos_p] is the first char after the directive
    //           - *fpar is set with the parameters read in the directive
  {
    typedef format_item  format_item_t;
    BOOST_ASSERT( pos_p != 0);
    std::string::size_type       &i1 = *pos_p,      
                                                        i0; 
    fpar->argN_ = format_item_t::argN_no_posit;  // if no positional-directive

    bool in_brackets=false;
    if(buf[i1]=='|')
      {
        in_brackets=true;
        if( ++i1 >= buf.size() ) {
          maybe_throw_exception(exceptions);
          return false;
        }
      }

    // the flag '0' would be picked as a digit for argument order, but here it's a flag :
    if(buf[i1]=='0') 
      goto parse_flags;

    // handle argument order (%2$d)  or possibly width specification: %2d
    i0 = i1;  // save position before digits
    while (i1 < buf.size() && wrap_isdigit(buf[i1], os))
      ++i1;
    if (i1!=i0) 
      {
        if( i1 >= buf.size() ) {
          maybe_throw_exception(exceptions);
          return false;
        }
        int n=str2int(buf,i0, os, int(0) );
        
        // %N% case : this is already the end of the directive
        if( buf[i1] == '%' ) 
          {
            fpar->argN_ = n-1;
            ++i1;
            if( in_brackets) 
              maybe_throw_exception(exceptions); 
              // but don't return.  maybe "%" was used in lieu of '$', so we go on.
            else return true;
          }

        if ( buf[i1]=='$' ) 
          {
            fpar->argN_ = n-1;
            ++i1;
          } 
        else  
          {
            // non-positionnal directive
            fpar->ref_state_.width_ = n;
            fpar->argN_  = format_item_t::argN_no_posit;
            goto parse_precision;
          }
      }
    
  parse_flags: 
    // handle flags
    while ( i1 <buf.size()) // as long as char is one of + - = # 0 l h   or ' '
      {  
        // misc switches
        switch (buf[i1]) 
          {
          case '\'' : break; // no effect yet. (painful to implement)
          case 'l':
          case 'h':  // short/long modifier : for printf-comaptibility (no action needed)
             break;
          case '-':
            fpar->ref_state_.flags_ |= std::ios::left;
            break;
          case '=':
            fpar->pad_scheme_ |= format_item_t::centered;
            break;
          case ' ':
            fpar->pad_scheme_ |= format_item_t::spacepad;
            break;
          case '+':
            fpar->ref_state_.flags_ |= std::ios::showpos;
            break;
          case '0':
            fpar->pad_scheme_ |= format_item_t::zeropad; 
            // need to know alignment before really setting flags,
            // so just add 'zeropad' flag for now, it will be processed later.
            break;
          case '#':
            fpar->ref_state_.flags_ |= std::ios::showpoint | std::ios::showbase;
            break;
          default:
            goto parse_width;
          }
        ++i1;
      } // loop on flag.
    if( i1>=buf.size()) {
      maybe_throw_exception(exceptions);
      return true; 
    }

  parse_width:
    // handle width spec
    skip_asterisk(buf, &i1, os); // skips 'asterisk fields' :  *, or *N$
    i0 = i1;  // save position before digits
    while (i1<buf.size() && wrap_isdigit(buf[i1], os))
      i1++;
    
    if (i1!=i0) 
      { fpar->ref_state_.width_ = str2int( buf,i0, os, std::streamsize(0) ); }

  parse_precision:
    if( i1>=buf.size()) { 
      maybe_throw_exception(exceptions);
      return true;
    }
    // handle precision spec
    if (buf[i1]=='.')  
      {
        ++i1;
        skip_asterisk(buf, &i1, os);
        i0 = i1;  // save position before digits
        while (i1<buf.size() && wrap_isdigit(buf[i1], os))
          ++i1;

        if(i1==i0)
          fpar->ref_state_.precision_ = 0;
        else 
          fpar->ref_state_.precision_ = str2int(buf,i0, os, std::streamsize(0) );
      }
    
    // handle  formatting-type flags :
    while( i1<buf.size() && 
           ( buf[i1]=='l' || buf[i1]=='L' || buf[i1]=='h') )
      ++i1;
    if( i1>=buf.size()) {
      maybe_throw_exception(exceptions);
      return true;
    }
    
    if( in_brackets && buf[i1]=='|' ) 
      {
        ++i1;
        return true;
      }
    switch (buf[i1])  
      {
      case 'X':
        fpar->ref_state_.flags_ |= std::ios::uppercase;
      case 'p': // pointer => set hex.
      case 'x':
        fpar->ref_state_.flags_ &= ~std::ios::basefield;
        fpar->ref_state_.flags_ |= std::ios::hex;
        break;
      
      case 'o':
        fpar->ref_state_.flags_ &= ~std::ios::basefield;
        fpar->ref_state_.flags_ |=  std::ios::oct;
        break;

      case 'E':
        fpar->ref_state_.flags_ |=  std::ios::uppercase;
      case 'e':
        fpar->ref_state_.flags_ &= ~std::ios::floatfield;
        fpar->ref_state_.flags_ |=  std::ios::scientific;

        fpar->ref_state_.flags_ &= ~std::ios::basefield;
        fpar->ref_state_.flags_ |=  std::ios::dec;
        break;
      
      case 'f':
        fpar->ref_state_.flags_ &= ~std::ios::floatfield;
        fpar->ref_state_.flags_ |=  std::ios::fixed;
      case 'u':
      case 'd':
      case 'i':
        fpar->ref_state_.flags_ &= ~std::ios::basefield;
        fpar->ref_state_.flags_ |=  std::ios::dec;
        break;

      case 'T':
        ++i1;
        if( i1 >= buf.size())
          maybe_throw_exception(exceptions);
        else
          fpar->ref_state_.fill_ = buf[i1];
        fpar->pad_scheme_ |= format_item_t::tabulation;
        fpar->argN_ = format_item_t::argN_tabulation; 
        break;
      case 't': 
        fpar->ref_state_.fill_ = ' ';
        fpar->pad_scheme_ |= format_item_t::tabulation;
        fpar->argN_ = format_item_t::argN_tabulation; 
        break;

      case 'G':
        fpar->ref_state_.flags_ |= std::ios::uppercase;
        break;
      case 'g': // 'g' conversion is default for floats.
        fpar->ref_state_.flags_ &= ~std::ios::basefield;
        fpar->ref_state_.flags_ |=  std::ios::dec;

        // CLEAR all floatield flags, so stream will CHOOSE
        fpar->ref_state_.flags_ &= ~std::ios::floatfield; 
        break;

      case 'C':
      case 'c': 
        fpar->truncate_ = 1;
        break;
      case 'S':
      case 's': 
        fpar->truncate_ = fpar->ref_state_.precision_;
        fpar->ref_state_.precision_ = -1;
        break;
      case 'n' :  
        fpar->argN_ = format_item_t::argN_ignored;
        break;
      default: 
        maybe_throw_exception(exceptions);
      }
    ++i1;

    if( in_brackets )
      {
        if( i1<buf.size() && buf[i1]=='|' ) 
          {
            ++i1;
            return true;
          }
        else  maybe_throw_exception(exceptions);
      }
    return true;
  }

} // detail namespace
} // io namespace


// -----------------------------------------------
//  format :: parse(..)

void basic_format::parse(const string_t & buf) 
  // parse the format-string
{
    using namespace std;
    const char arg_mark = '%';
    bool ordered_args=true; 
    int max_argN=-1;
    string_t::size_type i1=0;
    int num_items=0;
    
    // A: find upper_bound on num_items and allocates arrays
    i1=0; 
    while( (i1=buf.find(arg_mark,i1)) != string::npos ) 
    {
      if( i1+1 >= buf.size() ) {
        if(exceptions() & io::bad_format_string_bit)
          boost::throw_exception(io::bad_format_string()); // must not end in "bla bla %"
        else break; // stop there, ignore last '%'
      }
      if(buf[i1+1] == buf[i1] ) { i1+=2; continue; } // escaped "%%" / "##"
      ++i1;
      
      // in case of %N% directives, dont count it double (wastes allocations..) :
      while(i1 < buf.size() && io::detail::wrap_isdigit(buf[i1],oss_)) ++i1;
      if( i1 < buf.size() && buf[i1] == arg_mark ) ++ i1;

      ++num_items;
    }
    items_.assign( num_items, format_item_t() );
    
    // B: Now the real parsing of the format string :
    num_items=0;
    i1 = 0;
    string_t::size_type i0 = i1;
    bool special_things=false;
    int cur_it=0;
    while( (i1=buf.find(arg_mark,i1)) != string::npos ) 
    {
      string_t & piece = (cur_it==0) ? prefix_ : items_[cur_it-1].appendix_;

      if( buf[i1+1] == buf[i1] ) // escaped mark, '%%'
      {
        piece += buf.substr(i0, i1-i0) + buf[i1]; 
        i1+=2; i0=i1;
        continue; 
      }
      BOOST_ASSERT(  static_cast<unsigned int>(cur_it) < items_.size() || cur_it==0);

      if(i1!=i0) piece += buf.substr(i0, i1-i0);
      ++i1;
      
      bool parse_ok;
      parse_ok = io::detail::parse_printf_directive(buf, &i1, &items_[cur_it], oss_, exceptions());
      if( ! parse_ok ) continue; // the directive will be printed verbatim

      i0=i1;
      items_[cur_it].compute_states(); // process complex options, like zeropad, into stream params.

      int argN=items_[cur_it].argN_;
      if(argN == format_item_t::argN_ignored)
        continue;
      if(argN ==format_item_t::argN_no_posit)
        ordered_args=false;
      else if(argN == format_item_t::argN_tabulation) special_things=true;
      else if(argN > max_argN) max_argN = argN;
      ++num_items;
      ++cur_it;
    } // loop on %'s
    BOOST_ASSERT(cur_it == num_items);
    
    // store the final piece of string
    string_t & piece = (cur_it==0) ? prefix_ : items_[cur_it-1].appendix_;
    piece += buf.substr(i0);
    
    if( !ordered_args) 
    {
      if(max_argN >= 0 )  // dont mix positional with non-positionnal directives
        {
          if(exceptions() & io::bad_format_string_bit)
            boost::throw_exception(io::bad_format_string());
          // else do nothing. => positionnal arguments are processed as non-positionnal
        }
      // set things like it would have been with positional directives :
      int non_ordered_items = 0;
      for(int i=0; i< num_items; ++i)
        if(items_[i].argN_ == format_item_t::argN_no_posit) 
          {
            items_[i].argN_ = non_ordered_items;
            ++non_ordered_items;
          }
      max_argN = non_ordered_items-1;
    }
    
    // C: set some member data :
    items_.resize(num_items);

    if(special_things) style_ |= special_needs;
    num_args_ = max_argN + 1;
    if(ordered_args) style_ |=  ordered;
    else style_ &= ~ordered;
}

} // namespace boost


#endif //  BOOST_FORMAT_PARSING_HPP
