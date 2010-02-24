
#ifndef _LIBTERM_TERM_CLASS_FWD_HH
# define _LIBTERM_TERM_CLASS_FWD_HH

# include <set>
# include "visitor_fwd.hh"

namespace term
{
  // Performance issue: Abstract classes should be copied where fully
  // defined terms should be used with constant references.  An ATerm is
  // only composed by a pointer and should not use any virtual method.  By
  // following this rule, the compiler can optimize the size to the size of
  // its members (which is a pointer).

  class ATermImpl
  {
  public:
    ATermImpl();
    virtual ~ATermImpl();

  public:
    virtual ATerm accept(ATermVisitor& v) const = 0;
  };


  class ATerm
  {
  public:
    ATerm(const ATerm& t);
    ATerm();

  protected:
    ATerm(const ATermImpl* ptr);

  public:
    ATerm accept(ATermVisitor& v) const;

  public:
    bool operator== (const ATerm& rhs) const;
    bool operator!= (const ATerm& rhs) const;
    bool operator <(const ATerm& rhs) const;
    operator bool();

  public:
    const ATermImpl* get_ptr() const;

  protected:
    const ATermImpl* ptr_;
  };


  struct Term : public ATermImpl
  {
  public:
    typedef Term this_type;
    typedef ATerm term;

  public:
    bool operator <(const this_type&) const;
  };


  // [

// Declare the constructor
# define TRM_IMPL_CTR_DECL(Name, Base, Attributes, BaseArgs)            \
    Name ( TRM_SEPARATE_COMMA(                                          \
             TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)     \
         ) );

// Declare the make method which create an abstract term.
# define TRM_IMPL_MAKE_DECL(Name, Base, Attributes, BaseArgs)   \
    static                                                      \
    term                                                        \
    make(                                                       \
      TRM_SEPARATE_COMMA(                                       \
        TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)  \
      )                                                         \
    );

// Declare all the attributes of the current class.
# define TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)  \
  public:                                                       \
    TRM_APPLY(TRM_CONST_ABSTRACT_COPY_DECL, Attributes)


// A class which provide static method (no virtual) to access the
// implementation of the term.  This class is a wrapper over a pointer which
// should derivate from the ATerm class.
# define TRM_ABSTRACT_INTERFACE(Name, Base, Attributes, BaseArgs)       \
  class A ## Name : public A ## Base                                    \
  {                                                                     \
    typedef A ## Base parent;                                           \
                                                                        \
  public:                                                               \
    A ## Name (const A ## Name& t);                                     \
    A ## Name ();                                                       \
                                                                        \
  protected:                                                            \
    explicit A ## Name (const ATermImpl* ptr);                          \
  };

// Add the implementation class as a friend class to provide access to the
// private constructor.  This reduce interaction with the implementation of
// the terms.
# define TRM_ABSTRACT_FINAL(Name, Base, Attributes, BaseArgs)   \
  class A ## Name : public A ## Base                            \
  {                                                             \
    typedef A ## Base parent;                                   \
                                                                \
  public:                                                       \
    A ## Name (const A ## Name& t);                             \
    A ## Name ();                                               \
                                                                \
    const Name &                                                \
    operator* () const;                                         \
                                                                \
    const Name*                                                 \
    operator-> () const;                                        \
                                                                \
  protected:                                                    \
    explicit A ## Name (const ATermImpl* ptr);                  \
                                                                \
    friend class Name;                                          \
  };

# define TRM_IMPL_INTERFACE(Name, Base, Attributes, BaseArgs)   \
  class Name : public Base                                      \
  {                                                             \
    typedef Name this_type;                                     \
    typedef Base parent;                                        \
                                                                \
  public:                                                       \
    bool operator <(const Name& arg_rhs) const;                 \
                                                                \
  protected:                                                    \
    TRM_IMPL_CTR_DECL(Name, Base, Attributes, BaseArgs)         \
                                                                \
  public:                                                       \
    TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)       \
  };

# define TRM_IMPL_FINAL(Name, Base, Attributes, BaseArgs)       \
  class Name : public Base                                      \
  {                                                             \
    typedef Name this_type;                                     \
    typedef Base parent;                                        \
  public:                                                       \
    typedef A ## Name term;                                     \
    typedef std::set<this_type> term_set_type;                  \
                                                                \
  public:                                                       \
    TRM_IMPL_MAKE_DECL(Name, Base, Attributes, BaseArgs)        \
    ATerm accept(ATermVisitor& v) const;                        \
    bool operator <(const Name& arg_rhs) const;                 \
                                                                \
  protected:                                                    \
    TRM_IMPL_CTR_DECL(Name, Base, Attributes, BaseArgs)         \
                                                                \
  public:                                                       \
    TRM_ATTRIBUTE_DECLS(Name, Base, Attributes, BaseArgs)       \
                                                                \
  private:                                                      \
                                                                \
    static                                                      \
    const ATermImpl*                                            \
    get_identity(const this_type& t);                           \
                                                                \
    static                                                      \
    term_set_type& set_instance();                              \
  };


  TRM_GRAMMAR_NODES(TRM_ABSTRACT_INTERFACE, TRM_ABSTRACT_FINAL)
  TRM_GRAMMAR_NODES(TRM_IMPL_INTERFACE, TRM_IMPL_FINAL)

# undef TRM_ABSTRACT_INTERFACE
# undef TRM_ABSTRACT_FINAL
# undef TRM_IMPL_INTERFACE
# undef TRM_IMPL_FINAL

# undef TRM_IMPL_CTR_DECL
# undef TRM_ATTRIBUTE_DECLS
# undef TRM_IMPL_MAKE_DECL
  // ]


# define TRM_GENERIC_MAKE_DECL(Name, Base, Attributes, BaseArgs)        \
  Name :: term                                                          \
  make ## Name(                                                         \
    TRM_SEPARATE_COMMA(                                                 \
      TRM_MAP_ARRAY(TRM_CONST_ABSTRACT_COPY_ARG, Attributes)            \
    )                                                                   \
  );

  TRM_GRAMMAR_NODES(TRM_NIL_ARGS, TRM_GENERIC_MAKE_DECL)

# undef TRM_GENERIC_MAKE_DECL
}

#endif
