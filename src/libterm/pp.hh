
#ifndef _LIBTERM_PP_HH
# define _LIBTERM_PP_HH

# include <boost/preprocessor/tuple.hpp>
# include <boost/preprocessor/seq.hpp>
# include <boost/preprocessor/cat.hpp>
# include <boost/preprocessor/array.hpp>

// a few shortcuts to keep things clear.

# define TRM_NIL
# define TRM_NIL_ARGS(...)
# define TRM_EMPTY_ARRAY  (0, ())

# define TRM_SEQ_HEAD BOOST_PP_SEQ_HEAD
# define TRM_SEQ_TAIL BOOST_PP_SEQ_TAIL

# define TRM_ARRAY_DATA BOOST_PP_ARRAY_DATA
# define TRM_ARRAY_SIZE BOOST_PP_ARRAY_SIZE
# define TRM_ARRAY_ELEM BOOST_PP_ARRAY_ELEM
# define TRM_SEQ_TO_ARRAY BOOST_PP_SEQ_TO_ARRAY
# define TRM_ARRAY_TO_SEQ(Array)                                        \
  BOOST_PP_TUPLE_TO_SEQ(TRM_ARRAY_SIZE(Array), TRM_ARRAY_DATA(Array))

// TRM_EVAL macros are used to do the convertion of array to sequence and to
// provide a default case when the array cannot be converted into a
// sequence.  This case happens when you need to convert a zero size array.

# define TRM_EVAL_0(Macro1, Macro2, Array, Default) Default
# define TRM_EVAL_1(Macro1, Macro2, Array, Default)     \
  Macro1(Macro2, TRM_ARRAY_TO_SEQ(Array))

# define TRM_EVAL_2 TRM_EVAL_1
# define TRM_EVAL_3 TRM_EVAL_1
# define TRM_EVAL_4 TRM_EVAL_1

# define TRM_EVAL_II(eval) eval
# define TRM_EVAL_I(n, _) TRM_EVAL_ ## n
# define TRM_EVAL(Macro1, Macro2, Array, Default)                       \
  TRM_EVAL_II(TRM_EVAL_I Array (Macro1, Macro2, Array, Default))

# define TRM_DEFAULT1(D0)  D0

// Default TRM_<fun> are taking an array and return a sequence.  To keep the
// same type, you need to specify which type you expect to have.  The reason
// is that array support to have no values where sequence do not support it.
// On the contrary, all operations are well defined on sequences but none
// are defined on arrays.

# define TRM_MAP_HELPER(R, Macro, Elt) (Macro(Elt))
# define TRM_MAP_(Macro, Seq)                           \
  BOOST_PP_SEQ_FOR_EACH(TRM_MAP_HELPER, Macro, Seq)
# define TRM_MAP_SEQ(Macro, Seq)                \
  TRM_MAP_(Macro, Seq)
# define TRM_MAP_ARRAY_(Macro, Seq)             \
  TRM_SEQ_TO_ARRAY(TRM_MAP_SEQ(Macro, Seq))
# define TRM_MAP_ARRAY(Macro, Array)            \
  TRM_EVAL(TRM_MAP_ARRAY_, Macro, Array,        \
    TRM_DEFAULT1(TRM_EMPTY_ARRAY)               \
  )
# define TRM_MAP(Macro, Array)                  \
  TRM_EVAL(TRM_MAP_SEQ, Macro, Array,           \
    TRM_DEFAULT1(TRM_NIL)                       \
  )

// Apply a macro on all elements (array / sequence)

# define TRM_APPLY_HELPER(R, Macro, Elt) Macro(Elt)
# define TRM_APPLY_(Macro, Seq)                         \
  BOOST_PP_SEQ_FOR_EACH(TRM_APPLY_HELPER, Macro, Seq)
# define TRM_APPLY_SEQ(Macro, Seq)              \
  TRM_APPLY_(Macro, Seq)
# define TRM_APPLY(Macro, Array)                \
  TRM_EVAL(TRM_APPLY_, Macro, Array,            \
    TRM_DEFAULT1(TRM_NIL)                       \
  )

// Apply a macro on all elements (array / sequence) and separate them by a
// comma.

# define TRM_SEPARATE_COMMA_HELPER(Elt) , Elt
# define TRM_SEPARATE_COMMA_(_, Seq)                            \
  TRM_SEQ_HEAD(Seq)                                             \
  TRM_APPLY_SEQ(TRM_SEPARATE_COMMA_HELPER, TRM_SEQ_TAIL(Seq))
# define TRM_SEPARATE_COMMA_SEQ(Seq)            \
  TRM_SEPARATE_COMMA(TRM_SEQ_TO_ARRAY(Seq))
# define TRM_SEPARATE_COMMA(Array)                      \
  TRM_EVAL(TRM_SEPARATE_COMMA_, dummy, Array,           \
    TRM_DEFAULT1(TRM_NIL)     \
  )


#endif
