
#ifndef _LIBTERM_TERM_HH
# define _LIBTERM_TERM_HH

# include <set>

# include <boost/preprocessor/tuple.hpp>
# include <boost/preprocessor/seq.hpp>
# include <boost/preprocessor/cat.hpp>
# include <boost/preprocessor/array.hpp>


# define TRM_GRAMMAR_NODE_BINOP(Final, Name)                    \
  Final(                                                        \
    Name, Expr,                                                 \
    (2, (TRM_TYPE_TERM(Expr, lhs), TRM_TYPE_TERM(Expr, rhs))),  \
    (0,())                                                      \
  )

# define TRM_GRAMMAR_NODE_SINGLETON(Final, Name)        \
  Final(Name, Expr, (0,()), (0,()))

namespace term
{
  class ATerm;
  class ATermNil;

# define TRM_INTERFACE_DECLARE(Name, Base, Attributes, BaseArgs)        \
  template <typename T>                                                 \
  class Name;

# define TRM_FINAL_DECLARE(Name, Base, Attributes, BaseArgs)  \
  class Name;

  TRM_GRAMMAR_NODES(TRM_INTERFACE_DECLARE, TRM_FINAL_DECLARE)

# undef TRM_FINAL_DECLARE
# undef TRM_INTERFACE_DECLARE

  class ATermVisitor
  {
    virtual ATerm visit(const ATerm e) {
      return ATermNil;
    }

# define TRM_VISITOR(Name, Base, Attributes, BaseArgs)  \
    virtual ATerm visit(const Name e) {                 \
      return ATermNil;                                  \
    }

    TRM_GRAMMAR_NODES(TRM_VISITOR, TRM_VISITOR)
# undef TRM_VISITOR

  };


  // Performance issue: Abstract classes should be copied where fully
  // defined terms should be used with constant references.  An ATerm is
  // only composed by a pointer and should not use any virtual method.  By
  // following this rule, the compiler can optimize the size to the size of
  // its members (which is a pointer).

  class ATermImpl
  {
  public:
    ATermImplemntation()
    {
    }

    virtual ~ATermImplemntation()
    {
    }

  public:
    virtual ATerm accept(ATermVisitor& v) const = 0;
  };


  class ATerm
  {
  private:
    inline
    ATerm(ATermImpl *ptr)
      : ptr_(ptr)
    {
      assert(ptr_);
    }

  public:
    inline
    ATerm(ATerm t)
      : ptr_(t.ptr_)
    {
    }

  public:
    inline
    ATerm accept(ATermVisitor& v)
    {
      return ptr_->accept(v);
    }

  public:
    inline
    bool operator== (const ATerm& rhs) const
    {
      return ptr_ == rhs.ptr_;
    }

    inline
    bool operator!= (const ATerm& rhs) const
    {
      return ptr_ != rhs.ptr_;
    }

    inline
    bool operator <(const ATerm& rhs) const
    {
      return ptr_ < rhs.ptr_;
    }

  private:
    const void* ptr_;
  };

  class ATermNul : public ATerm
  {
  public:
    ATermNil()
      : ATerm(0)
    {
    }
  };


  template <typename T>
  struct Term : public ATermImpl
  {
    typedef std::set<T> term_set_type;

  public:
    inline
    bool operator <(const Function& rhs)
    {
      return false;
    }

    static inline
    bool
    matchTerm(const ATerm t)
    {
      return true;
    }

  private:
    inline
    static
    ATermImpl*
    get_identity(const T& t)
    {
      return static_cast<ATermImpl*>(&*t.set_instance().insert(t).first)
    }

    inline
    term_set_type& set_instance()
    {
      static term_set_type set();
      return set;
    };
  };


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
# define TRM_SEPARATE_COMMA_(_, Seq)                                    \
  TRM_SEQ_HEAD(Seq)
  TRM_APPLY_SEQ(TRM_SEPARATE_COMMA_HELPER, TRM_SEQ_TAIL(Seq))
# define TRM_SEPARATE_COMMA_SEQ(Seq)            \
  TRM_SEPARATE_COMMA(TRM_SEQ_TO_ARRAY(Seq))
# define TRM_SEPARATE_COMMA(Array)                      \
  TRM_EVAL(TRM_SEPARATE_COMMA_, dummy, Array,           \
    TRM_DEFAULT1(TRM_NIL)     \
  )

// These macro are used to define how to manipulate each argument.  If the
// argument should be given by reference or by copy.  You should use
// references when the element goes over a specific size and if you accept
// to see it living on the stack.  You should use copies when the element is
// small and if you prefer to have it inside a register.

# define TRM_TYPE_REF(Type, Name) (const Type&, Type &, Name)
# define TRM_TYPE_COPY(Type, Name) (const Type, Type &, Name)
# define TRM_TYPE_TERM(Type, Name) TRM_TYPE_COPY(A ## Type, Name)

// Handle the different usage of the variables.  Arguments are suffixed with
// a '_' and attributes are not.

# define TRM_CONST_ABSTRACT_COPY_DECL_(Copy, Ref, Name) Copy Name;
# define TRM_CONST_ABSTRACT_COPY_ARG_(Copy, Ref, Name)  Copy Name ## _
# define TRM_ABSTRACT_REF_ARG_(Copy, Ref, Name)  Ref Name ## _
# define TRM_INIT_ATTRIBUTES_(Copy, Ref, Name)  Name (Name ## _)
# define TRM_ARGUMENTS_(Copy, Ref, Name)  Name ## _
# define TRM_COPY_IN_ARG_(Copy, Ref, Name)  Name ## _ = Name;
# define TRM_LESS_RHS_OR_(Copy, Ref, Name)  Name < rhs. Name ||

// These macro are shortcuts used to remove extra parenthesies added by
// TRM_TYPE_* macros.  Without such parenthesies TRM_APPLY_HELPER won't be
// able to give the argument to these macros and arrays won't be well
// formed.

# define TRM_CONST_ABSTRACT_COPY_DECL(Elt) TRM_CONST_ABSTRACT_COPY_DECL_ Elt
# define TRM_CONST_ABSTRACT_COPY_ARG(Elt) TRM_CONST_ABSTRACT_COPY_ARG_ Elt
# define TRM_ABSTRACT_REF_ARG(Elt) TRM_ABSTRACT_REF_ARG_ Elt
# define TRM_INIT_ATTRIBUTES(Elt) TRM_INIT_ATTRIBUTES_ Elt
# define TRM_ARGUMENTS(Elt) TRM_ARGUMENTS_ Elt
# define TRM_COPY_IN_ARG(Elt) TRM_COPY_IN_ARG_ Elt
# define TRM_LESS_RHS_OR(Elt) TRM_LESS_RHS_OR_ Elt


// Add the implementation class as a friend class to provide access to the
// private constructor.  This reduce interaction with the implementation of
// the terms.
# define TRM_ADD_FRIEND(Name, Base, Attributes, BaseArgs)       \
  friend Name;


// Initialized all attributes of the current class and call the base class
// with the expected arguments.
# define TRM_GRAMMAR_NODE_CTR(Name, Base, Attributes, BaseArgs)         \
  private:                                                              \
    Name ( TRM_SEPARATE_COMMA(                                          \
             TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)     \
           ) )                                                          \
    : TRM_SEPARATE_COMMA_SEQ(                                           \
        TRM_MAP(TRM_INIT_ATTRIBUTES, Attributes)                        \
        ( parent(TRM_SEPARATE_COMMA(                                    \
            TRM_MAP_ARRAY(TRM_ARGUMENTS, BaseArgs)                      \
          ))                                                            \
        )                                                               \
      )                                                                 \
    {                                                                   \
    }



// This method is used to provide maximal sharing among node of the same
// types.  This create the class with the private constructor and register
// it if this is not already done.  It returns a term which has a small
// memory impact (only a pointer) which refers to the created element.
# define TRM_GRAMMAR_NODE_MAKE_METHOD(Name, Base, Attributes, BaseArgs) \
  public:                                                               \
    static inline                                                       \
    term                                                                \
    make ## Name(                                                       \
      TRM_SEPARATE_COMMA(                                               \
        TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)          \
      )                                                                 \
    )                                                                   \
    {                                                                   \
      return term(get_identity(this_type(                               \
        TRM_SEPARATE_COMMA(                                             \
          TRM_MAP_ARRAY(TRM_ARGUMENTS, Attributes)                      \
        )                                                               \
      )));                                                              \
    }


// This method provides backward compatibility but it is inefficient.  The
// macro produce a match function which deconstruct the node by copying all
// attributes values into references given as arguments of this function.
// The first argument is the term which has to be deconstructed.
# define TRM_GRAMMAR_NODE_MATCH_METHOD(Name, Base, Attributes, BaseArgs) \
  public:                                                               \
    static inline                                                       \
    bool                                                                \
    match ## Name(                                                      \
      TRM_SEPARATE_COMMA_SEQ(                                           \
        ( const ATerm t )                                               \
        TRM_MAP(TRM_ABSTRACT_REF_ARG, Attributes)                       \
      )                                                                 \
    )                                                                   \
    {                                                                   \
      this_type* ptr;                                                   \
      if (ptr = dynamic_cast<this_type*>(t.ptr_))                       \
      {                                                                 \
        TRM_APPLY(TRM_COPY_IN_ARG, Attributes);                         \
        return parent::match ## Base (                                  \
          TRM_SEPARATE_COMMA_SEQ(                                       \
            ( t )                                                       \
            TRM_MAP(TRM_ARGUMENTS, BaseArgs)                            \
          )                                                             \
        );                                                              \
      }                                                                 \
      return false;                                                     \
    }


// This operator is used for checking if the current ellement has not been
// create before.  It compares each attributes of the current class and
// delegate other attribute comparison to his parent class.
# define TRM_LESS_GRAMMAR_NODE_OP(Name, Base, Attributes, BaseArgs)     \
  public:                                                               \
    inline                                                              \
    bool operator <(const Name& rhs)                                    \
    {                                                                   \
      return TRM_APPLY(TRM_LESS_RHS_OR, Attributes)                     \
        parent::operator < (rhs);                                       \
    }


// Declare all the attributes of the current class.
# define TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)  \
  public:                                                       \
    TRM_APPLY(TRM_CONST_ABSTRACT_COPY_DECL, Attributes)


// A class which provide static method (no virtual) to access the
// implementation of the term.  This class is a wrapper over a pointer which
// should derivate from the ATerm class.
# define TRM_ABSTRACT_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs, NoTemplate) \
  class A ## Name : public A ## Base                                    \
  {                                                                     \
    typedef A ## Base parent;                                           \
                                                                        \
  public:                                                               \
    A ## Name (A ## Name t)                                             \
      : parent(t)                                                       \
    {                                                                   \
    }                                                                   \
                                                                        \
    inline                                                              \
    const Name &                                                        \
    operator() () const                                                 \
    {                                                                   \
      return *static_cast<const Name *>(ptr_);                          \
    }                                                                   \
                                                                        \
  private:                                                              \
    explicit A ## Name (ATermImpl *ptr)                                 \
      : parent(ptr)                                                     \
    {                                                                   \
    }                                                                   \
                                                                        \
    NoTemplate(Name, Base, Attributes, BaseArgs)                        \
  };


# define TRM_INTERFACE_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs)   \
  template <typename T>                                                 \
  class Name : public Base<T>                                           \
  {                                                                     \
    typedef Base<T> parent;                                             \
                                                                        \
    TRM_GRAMMAR_NODE_CTR(Name, Base, Attributes, BaseArgs)              \
    TRM_GRAMMAR_NODE_MATCH_METHOD(Name, Base, Attributes, BaseArgs)     \
    TRM_LESS_GRAMMAR_NODE_OP(Name, Base, Attributes, BaseArgs)          \
    TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)               \
  };


# define TRM_FINAL_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs)       \
  class Name : public Base<Name>                                        \
  {                                                                     \
    typedef Base<Name> parent;                                          \
    typedef A ## Name term;                                             \
    typedef Name this_type;                                             \
                                                                        \
    TRM_GRAMMAR_NODE_CTR(Name, Base, Attributes, BaseArgs)              \
    TRM_GRAMMAR_NODE_MAKE_METHOD(Name, Base, Attributes, BaseArgs)      \
    TRM_GRAMMAR_NODE_MATCH_METHOD(Name, Base, Attributes, BaseArgs)     \
                                                                        \
  public:                                                               \
    ATerm accept(ATermVisitor& v) const                                 \
    {                                                                   \
      return v.visit(*this);                                            \
    }                                                                   \
                                                                        \
    TRM_LESS_GRAMMAR_NODE_OP(Name, Base, Attributes, BaseArgs)          \
    TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)               \
  };                                                                    \
                                                                        \
  using Name::make ## Name;                                             \
  using Name::match ## Name;


# define TRM_INTERFACE(Name, Base, Attributes, BaseArgs)                \
  TRM_ABSTRACT_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs, TRM_NIL_ARGS) \
  TRM_INTERFACE_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs)

# define TRM_FINAL(Name, Base, Attributes, BaseArgs)                    \
  TRM_ABSTRACT_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs, TRM_ADD_FRIEND) \
  TRM_FINAL_GRAMMAR_NODE(Name, Base, Attributes, BaseArgs)

  TRM_GRAMMAR_NODES(TRM_INTERFACE, TRM_FINAL)

# undef TRM_FINAL
# undef TRM_INTERFACE

  /*
  class AExpr : public ATerm
  {
    typedef ATerm parent;

  public:
    AExpr(AExpr t)
      : parent(t)
    {
    }

    inline
    const Expr&
    operator() () const
    {
      return *static_cast<const Expr*>(ptr_);
    }

  private:
    explicit AExpr(ATermImpl *ptr)
      : parent(ptr)
    {
    }
  };

  template <typename T>
  class Expr : public Term<T>
  {
    typedef Expr<Function> parent;

  public:
    inline
    bool operator <(const Function& rhs)
    {
      return
        parent::operator < (rhs);
    }
  };


  class Function;
  class AFunction : public AExpr
  {
    typedef AExpr parent;

  public:
    AFunction(AFunction t)
      : parent(t)
    {
    }

    inline
    const Function&
    operator() () const
    {
      return *static_cast<const Function*>(ptr_);
    }

  private:
    explicit AFunction(ATermImpl *ptr)
      : parent(ptr)
    {
    }

    friend Function;
  };

  class Function : public Expr<Function>
  {
    typedef Expr<Function> parent;
    typedef AFunction term;
    typedef Function this_type;

  private:
    Function(const APattern a1, const AExpr a2, const APos a3)
      : pattern(a1), expr(a2), pos(a3)
    {
    }

  public:
    // provide a function to create an ATerm without any reference to the
    // ATerm implementation from the user point of view.
    inline
    static
    AFunction
    makeFunction(const APattern a1, const AExpr a2, const APos a3)
    {
      return AFunction(get_identity(Function(a1, a2, a3)));
    }

    // this function provides backward compatibility but it is inefficient.
    inline
    static
    bool
    matchFunction(const ATerm t, APattern &a1, AExpr &a2, APos &a3)
    {
      Function* ptr;
      if (ptr = dynamic_cast<this_type*>(t.ptr_))
      {
        a1 = pattern;
        a2 = expr;
        a3 = pos;
        return true;
      }
      return false;
    }

  public:
    void accept(ATermVisitor& v) const
    {
      v.visit(*this);
    }

    // This operator is used for checking if the current ellement has not
    // been create before.
    bool operator <(const Function& rhs)
    {
      return
        pos < rhs.pos ||
        expr < rhs.expr ||
        pattern < rhs.pattern ||
        parent::operator < (rhs);
    }

    const APattern pattern;
    const AExpr expr;
    const APos pos;
  };

  using Function::makeFunction;
  using Function::matchFunction;
  */


  template <typename T>
  class getVisitor : ATermNoOpVisitor
  {
  public:
    getVisitor(ATerm t)
    {
      t.accept(*this);
    }

    ATerm visit(const T& t)
    {
      res = std::pair(true, &t);
      return ATermNul();
    }

    std::pair<bool, T*> res;
  };

  template <typename T>
  std::pair<bool, T*>
  is_a(Aterm t)
  {
    getVisitor v(t);
    return v.res;
  }

  template <typename T>
  T
  as(Aterm t)
  {
    getVisitor v(t);
    return v.res;
  }
}

#endif
