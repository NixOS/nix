#ifndef BOOST_THROW_EXCEPTION_HPP_INCLUDED
#define BOOST_THROW_EXCEPTION_HPP_INCLUDED

// MS compatible compilers support #pragma once

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma once
#endif

//
//  boost/throw_exception.hpp
//
//  Copyright (c) 2002 Peter Dimov and Multi Media Ltd.
//
//  Permission to copy, use, modify, sell and distribute this software
//  is granted provided this copyright notice appears in all copies.
//  This software is provided "as is" without express or implied
//  warranty, and with no claim as to its suitability for any purpose.
//
//  http://www.boost.org/libs/utility/throw_exception.html
//

//#include <boost/config.hpp>

#ifdef BOOST_NO_EXCEPTIONS
# include <exception>
#endif

namespace boost
{

#ifdef BOOST_NO_EXCEPTIONS

void throw_exception(std::exception const & e); // user defined

#else

template<class E> void throw_exception(E const & e)
{
    throw e;
}

#endif

} // namespace boost

#endif // #ifndef BOOST_THROW_EXCEPTION_HPP_INCLUDED
