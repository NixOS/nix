
#ifndef _LIBTERM_TERM_HH
# define _LIBTERM_TERM_HH

# include "term.hh"
# include "visitor.hh"

namespace term
{
  inline
  ATermImpl::ATermImpl()
  {
  }

  inline
  ATermImpl::~ATermImpl()
  {
  }


  inline
  ATerm::ATerm(const ATerm& t)
    : ptr_(t.ptr_)
  {
  }

  inline
  ATerm::ATerm()
    : ptr_(0)
  {
  }

  inline
  ATerm::ATerm(const ATermImpl* ptr)
    : ptr_(ptr)
  {
  }

  inline
  ATerm
  ATerm::accept(ATermVisitor& v) const
  {
    return ptr_->accept(v);
  }

  inline
  bool
  ATerm::operator== (const ATerm& rhs) const
  {
    return ptr_ == rhs.ptr_;
  }

  inline
  bool
  ATerm::operator!= (const ATerm& rhs) const
  {
    return ptr_ != rhs.ptr_;
  }

  inline
  bool
  ATerm::operator <(const ATerm& rhs) const
  {
    return ptr_ < rhs.ptr_;
  }

  inline
  ATerm::operator bool()
  {
    return ptr_;
  }

  inline
  const ATermImpl*
  ATerm::get_ptr() const
  {
    return ptr_;
  }


  inline
  bool
  Term::operator <(const this_type&) const
  {
    return false;
  }


// define the contructors of the Abstract terms.  These constructors are
// just delegating the work to the ATerm constructors.
# define TRM_ABSTRACT_CTR(Name, Base, Attributes, BaseArgs)     \
  inline                                                        \
  A ## Name :: A ## Name (const A ## Name& t) :                 \
    parent(t)                                                   \
  {                                                             \
  }                                                             \
                                                                \
  inline                                                        \
  A ## Name :: A ## Name () :                                   \
    parent()                                                    \
  {                                                             \
  }                                                             \
                                                                \
  inline                                                        \
  A ## Name :: A ## Name (const ATermImpl* ptr) :               \
    parent(ptr)                                                 \
  {                                                             \
  }

  TRM_GRAMMAR_NODES(TRM_ABSTRACT_CTR, TRM_ABSTRACT_CTR)

# undef TRM_ABSTRACT_CTR


// Give acces to the implementation in order to get access to each field of
// the term.
# define TRM_ABSTRACT_FINAL_ACCESS(Name, Base, Attributes, BaseArgs)    \
  inline                                                                \
  const Name &                                                          \
  A ## Name :: operator* () const                                       \
  {                                                                     \
    return *static_cast<const Name *>(ptr_);                            \
  }                                                                     \
                                                                        \
  inline                                                                \
  const Name*                                                           \
  A ## Name :: operator-> () const                                      \
  {                                                                     \
    return static_cast<const Name *>(ptr_);                             \
  }

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_ABSTRACT_FINAL_ACCESS)

# undef TRM_ABSTRACT_FINAL_ACCESS



// Initialized all attributes of the current class and call the base class
// with the expected arguments.
# define TRM_IMPL_CTR(Name, Base, Attributes, BaseArgs)         \
  inline                                                        \
  Name :: Name (                                                \
    TRM_SEPARATE_COMMA(                                         \
      TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)    \
    )                                                           \
  )                                                             \
    : TRM_SEPARATE_COMMA_SEQ(                                   \
        TRM_MAP(TRM_INIT_ATTRIBUTES, Attributes)                \
        ( parent(TRM_SEPARATE_COMMA(                            \
            TRM_MAP_ARRAY(TRM_ARGUMENTS, BaseArgs)              \
          ))                                                    \
        )                                                       \
      )                                                         \
  {                                                             \
  }

  TRM_GRAMMAR_NODES(TRM_IMPL_CTR, TRM_IMPL_CTR)

# undef TRM_IMPL_CTR


// This operator is used for checking if the current ellement has not been
// create before.  It compares each attributes of the current class and
// delegate other attribute comparison to his parent class.
# define TRM_IMPL_LESS(Name, Base, Attributes, BaseArgs)        \
  inline                                                        \
  bool                                                          \
  Name :: operator <(const Name& arg_rhs) const                 \
  {                                                             \
    return TRM_APPLY(TRM_LESS_RHS_OR, Attributes)               \
      parent::operator < (arg_rhs);                             \
  }

  TRM_GRAMMAR_NODES(TRM_IMPL_LESS, TRM_IMPL_LESS)

# undef TRM_IMPL_LESS


// This method is used to provide maximal sharing among node of the same
// types.  This create the class with the private constructor and register
// it if this is not already done.  It returns a term which has a small
// memory impact (only a pointer) which refers to the created element.
# define TRM_IMPL_FINAL_MAKE(Name, Base, Attributes, BaseArgs)  \
  inline                                                        \
  Name :: term                                                  \
  Name :: make(                                                 \
    TRM_SEPARATE_COMMA(                                         \
      TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)    \
    )                                                           \
  )                                                             \
  {                                                             \
    return term(get_identity(this_type(                         \
      TRM_SEPARATE_COMMA(                                       \
        TRM_MAP_ARRAY(TRM_ARGUMENTS, Attributes)                \
      )                                                         \
    )));                                                        \
  }

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_IMPL_FINAL_MAKE)

# undef TRM_IMPL_FINAL_MAKE


// Handle visit from the ATermVisitor and forward them to the Abstract term
// of the current term.
# define TRM_IMPL_FINAL_VISIT(Name, Base, Attributes, BaseArgs)         \
  inline                                                                \
  ATerm                                                                 \
  Name :: accept(ATermVisitor& v) const                                 \
  {                                                                     \
    return v.visit(term(this));                                         \
  }

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_IMPL_FINAL_VISIT)

# undef TRM_IMPL_FINAL_VISIT


// These functions are used to provide maximal sharing between the terms.
// All terms are stored inside a set only the address the element contained
// in the set is used.
# define TRM_IMPL_FINAL_SHARED(Name, Base, Attributes, BaseArgs)        \
  inline                                                                \
  const ATermImpl*                                                      \
  Name :: get_identity(const this_type& t)                              \
  {                                                                     \
    return static_cast<const ATermImpl*>(                               \
      &*set_instance().insert(t).first                                  \
    );                                                                  \
  }                                                                     \
                                                                        \
  inline                                                                \
  Name :: term_set_type&                                                \
  Name :: set_instance()                                                \
  {                                                                     \
    static term_set_type set;                                           \
    return set;                                                         \
  }

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_IMPL_FINAL_SHARED)

# undef TRM_IMPL_FINAL_SHARED


// Improve user experience by providing function to create Abstract terms
// without any manipulation of the implementation class names.
# define TRM_GENERIC_MAKE(Name, Base, Attributes, BaseArgs)     \
  inline                                                        \
  Name :: term                                                  \
  make ## Name(                                                 \
    TRM_SEPARATE_COMMA(                                         \
      TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)    \
    )                                                           \
  )                                                             \
  {                                                             \
    return Name :: make(                                        \
      TRM_SEPARATE_COMMA(                                       \
        TRM_MAP_ARRAY(TRM_ARGUMENTS, Attributes)                \
      )                                                         \
    );                                                          \
  }

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_GENERIC_MAKE)

# undef TRM_GENERIC_MAKE

}

#endif
