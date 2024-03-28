//     Copyright Toru Niina 2019.
// Distributed under the MIT License.
#ifndef TOML11_COMMENTS_HPP
#define TOML11_COMMENTS_HPP
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef TOML11_PRESERVE_COMMENTS_BY_DEFAULT
#  define TOML11_DEFAULT_COMMENT_STRATEGY ::toml::preserve_comments
#else
#  define TOML11_DEFAULT_COMMENT_STRATEGY ::toml::discard_comments
#endif

// This file provides mainly two classes, `preserve_comments` and `discard_comments`.
// Those two are a container that have the same interface as `std::vector<std::string>`
// but bahaves in the opposite way. `preserve_comments` is just the same as
// `std::vector<std::string>` and each `std::string` corresponds to a comment line.
// Conversely, `discard_comments` discards all the strings and ignores everything
// assigned in it. `discard_comments` is always empty and you will encounter an
// error whenever you access to the element.
namespace toml
{
struct discard_comments; // forward decl

// use it in the following way
//
// const toml::basic_value<toml::preserve_comments> data =
//     toml::parse<toml::preserve_comments>("example.toml");
//
// the interface is almost the same as std::vector<std::string>.
struct preserve_comments
{
    // `container_type` is not provided in discard_comments.
    // do not use this inner-type in a generic code.
    using container_type         = std::vector<std::string>;

    using size_type              = container_type::size_type;
    using difference_type        = container_type::difference_type;
    using value_type             = container_type::value_type;
    using reference              = container_type::reference;
    using const_reference        = container_type::const_reference;
    using pointer                = container_type::pointer;
    using const_pointer          = container_type::const_pointer;
    using iterator               = container_type::iterator;
    using const_iterator         = container_type::const_iterator;
    using reverse_iterator       = container_type::reverse_iterator;
    using const_reverse_iterator = container_type::const_reverse_iterator;

    preserve_comments()  = default;
    ~preserve_comments() = default;
    preserve_comments(preserve_comments const&) = default;
    preserve_comments(preserve_comments &&)     = default;
    preserve_comments& operator=(preserve_comments const&) = default;
    preserve_comments& operator=(preserve_comments &&)     = default;

    explicit preserve_comments(const std::vector<std::string>& c): comments(c){}
    explicit preserve_comments(std::vector<std::string>&& c)
        : comments(std::move(c))
    {}
    preserve_comments& operator=(const std::vector<std::string>& c)
    {
        comments = c;
        return *this;
    }
    preserve_comments& operator=(std::vector<std::string>&& c)
    {
        comments = std::move(c);
        return *this;
    }

    explicit preserve_comments(const discard_comments&) {}

    explicit preserve_comments(size_type n): comments(n) {}
    preserve_comments(size_type n, const std::string& x): comments(n, x) {}
    preserve_comments(std::initializer_list<std::string> x): comments(x) {}
    template<typename InputIterator>
    preserve_comments(InputIterator first, InputIterator last)
        : comments(first, last)
    {}

    template<typename InputIterator>
    void assign(InputIterator first, InputIterator last) {comments.assign(first, last);}
    void assign(std::initializer_list<std::string> ini)  {comments.assign(ini);}
    void assign(size_type n, const std::string& val)     {comments.assign(n, val);}

    // Related to the issue #97.
    //
    // It is known that `std::vector::insert` and `std::vector::erase` in
    // the standard library implementation included in GCC 4.8.5 takes
    // `std::vector::iterator` instead of `std::vector::const_iterator`.
    // Because of the const-correctness, we cannot convert a `const_iterator` to
    // an `iterator`. It causes compilation error in GCC 4.8.5.
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__) && !defined(__clang__)
#  if (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) <= 40805
#    define TOML11_WORKAROUND_GCC_4_8_X_STANDARD_LIBRARY_IMPLEMENTATION
#  endif
#endif

#ifdef TOML11_WORKAROUND_GCC_4_8_X_STANDARD_LIBRARY_IMPLEMENTATION
    iterator insert(iterator p, const std::string& x)
    {
        return comments.insert(p, x);
    }
    iterator insert(iterator p, std::string&&      x)
    {
        return comments.insert(p, std::move(x));
    }
    void insert(iterator p, size_type n, const std::string& x)
    {
        return comments.insert(p, n, x);
    }
    template<typename InputIterator>
    void insert(iterator p, InputIterator first, InputIterator last)
    {
        return comments.insert(p, first, last);
    }
    void insert(iterator p, std::initializer_list<std::string> ini)
    {
        return comments.insert(p, ini);
    }

    template<typename ... Ts>
    iterator emplace(iterator p, Ts&& ... args)
    {
        return comments.emplace(p, std::forward<Ts>(args)...);
    }

    iterator erase(iterator pos) {return comments.erase(pos);}
    iterator erase(iterator first, iterator last)
    {
        return comments.erase(first, last);
    }
#else
    iterator insert(const_iterator p, const std::string& x)
    {
        return comments.insert(p, x);
    }
    iterator insert(const_iterator p, std::string&&      x)
    {
        return comments.insert(p, std::move(x));
    }
    iterator insert(const_iterator p, size_type n, const std::string& x)
    {
        return comments.insert(p, n, x);
    }
    template<typename InputIterator>
    iterator insert(const_iterator p, InputIterator first, InputIterator last)
    {
        return comments.insert(p, first, last);
    }
    iterator insert(const_iterator p, std::initializer_list<std::string> ini)
    {
        return comments.insert(p, ini);
    }

    template<typename ... Ts>
    iterator emplace(const_iterator p, Ts&& ... args)
    {
        return comments.emplace(p, std::forward<Ts>(args)...);
    }

    iterator erase(const_iterator pos) {return comments.erase(pos);}
    iterator erase(const_iterator first, const_iterator last)
    {
        return comments.erase(first, last);
    }
#endif

    void swap(preserve_comments& other) {comments.swap(other.comments);}

    void push_back(const std::string& v) {comments.push_back(v);}
    void push_back(std::string&&      v) {comments.push_back(std::move(v));}
    void pop_back()                      {comments.pop_back();}

    template<typename ... Ts>
    void emplace_back(Ts&& ... args) {comments.emplace_back(std::forward<Ts>(args)...);}

    void clear() {comments.clear();}

    size_type size()     const noexcept {return comments.size();}
    size_type max_size() const noexcept {return comments.max_size();}
    size_type capacity() const noexcept {return comments.capacity();}
    bool      empty()    const noexcept {return comments.empty();}

    void reserve(size_type n)                      {comments.reserve(n);}
    void resize(size_type n)                       {comments.resize(n);}
    void resize(size_type n, const std::string& c) {comments.resize(n, c);}
    void shrink_to_fit()                           {comments.shrink_to_fit();}

    reference       operator[](const size_type n)       noexcept {return comments[n];}
    const_reference operator[](const size_type n) const noexcept {return comments[n];}
    reference       at(const size_type n)       {return comments.at(n);}
    const_reference at(const size_type n) const {return comments.at(n);}
    reference       front()       noexcept {return comments.front();}
    const_reference front() const noexcept {return comments.front();}
    reference       back()        noexcept {return comments.back();}
    const_reference back()  const noexcept {return comments.back();}

    pointer         data()        noexcept {return comments.data();}
    const_pointer   data()  const noexcept {return comments.data();}

    iterator       begin()        noexcept {return comments.begin();}
    iterator       end()          noexcept {return comments.end();}
    const_iterator begin()  const noexcept {return comments.begin();}
    const_iterator end()    const noexcept {return comments.end();}
    const_iterator cbegin() const noexcept {return comments.cbegin();}
    const_iterator cend()   const noexcept {return comments.cend();}

    reverse_iterator       rbegin()        noexcept {return comments.rbegin();}
    reverse_iterator       rend()          noexcept {return comments.rend();}
    const_reverse_iterator rbegin()  const noexcept {return comments.rbegin();}
    const_reverse_iterator rend()    const noexcept {return comments.rend();}
    const_reverse_iterator crbegin() const noexcept {return comments.crbegin();}
    const_reverse_iterator crend()   const noexcept {return comments.crend();}

    friend bool operator==(const preserve_comments&, const preserve_comments&);
    friend bool operator!=(const preserve_comments&, const preserve_comments&);
    friend bool operator< (const preserve_comments&, const preserve_comments&);
    friend bool operator<=(const preserve_comments&, const preserve_comments&);
    friend bool operator> (const preserve_comments&, const preserve_comments&);
    friend bool operator>=(const preserve_comments&, const preserve_comments&);

    friend void swap(preserve_comments&, std::vector<std::string>&);
    friend void swap(std::vector<std::string>&, preserve_comments&);

  private:

    container_type comments;
};

inline bool operator==(const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments == rhs.comments;}
inline bool operator!=(const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments != rhs.comments;}
inline bool operator< (const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments <  rhs.comments;}
inline bool operator<=(const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments <= rhs.comments;}
inline bool operator> (const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments >  rhs.comments;}
inline bool operator>=(const preserve_comments& lhs, const preserve_comments& rhs) {return lhs.comments >= rhs.comments;}

inline void swap(preserve_comments& lhs, preserve_comments& rhs)
{
    lhs.swap(rhs);
    return;
}
inline void swap(preserve_comments& lhs, std::vector<std::string>& rhs)
{
    lhs.comments.swap(rhs);
    return;
}
inline void swap(std::vector<std::string>& lhs, preserve_comments& rhs)
{
    lhs.swap(rhs.comments);
    return;
}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const preserve_comments& com)
{
    for(const auto& c : com)
    {
        os << '#' << c << '\n';
    }
    return os;
}

namespace detail
{

// To provide the same interface with `preserve_comments`, `discard_comments`
// should have an iterator. But it does not contain anything, so we need to
// add an iterator that points nothing.
//
// It always points null, so DO NOT unwrap this iterator. It always crashes
// your program.
template<typename T, bool is_const>
struct empty_iterator
{
    using value_type        = T;
    using reference_type    = typename std::conditional<is_const, T const&, T&>::type;
    using pointer_type      = typename std::conditional<is_const, T const*, T*>::type;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    empty_iterator()  = default;
    ~empty_iterator() = default;
    empty_iterator(empty_iterator const&) = default;
    empty_iterator(empty_iterator &&)     = default;
    empty_iterator& operator=(empty_iterator const&) = default;
    empty_iterator& operator=(empty_iterator &&)     = default;

    // DO NOT call these operators.
    reference_type operator*()  const noexcept {std::terminate();}
    pointer_type   operator->() const noexcept {return nullptr;}
    reference_type operator[](difference_type) const noexcept {return this->operator*();}

    // These operators do nothing.
    empty_iterator& operator++()    noexcept {return *this;}
    empty_iterator  operator++(int) noexcept {return *this;}
    empty_iterator& operator--()    noexcept {return *this;}
    empty_iterator  operator--(int) noexcept {return *this;}

    empty_iterator& operator+=(difference_type) noexcept {return *this;}
    empty_iterator& operator-=(difference_type) noexcept {return *this;}

    empty_iterator  operator+(difference_type) const noexcept {return *this;}
    empty_iterator  operator-(difference_type) const noexcept {return *this;}
};

template<typename T, bool C>
bool operator==(const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return true;}
template<typename T, bool C>
bool operator!=(const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return false;}
template<typename T, bool C>
bool operator< (const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return false;}
template<typename T, bool C>
bool operator<=(const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return true;}
template<typename T, bool C>
bool operator> (const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return false;}
template<typename T, bool C>
bool operator>=(const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return true;}

template<typename T, bool C>
typename empty_iterator<T, C>::difference_type
operator-(const empty_iterator<T, C>&, const empty_iterator<T, C>&) noexcept {return 0;}

template<typename T, bool C>
empty_iterator<T, C>
operator+(typename empty_iterator<T, C>::difference_type, const empty_iterator<T, C>& rhs) noexcept {return rhs;}
template<typename T, bool C>
empty_iterator<T, C>
operator+(const empty_iterator<T, C>& lhs, typename empty_iterator<T, C>::difference_type) noexcept {return lhs;}

} // detail

// The default comment type. It discards all the comments. It requires only one
// byte to contain, so the memory footprint is smaller than preserve_comments.
//
// It just ignores `push_back`, `insert`, `erase`, and any other modifications.
// IT always returns size() == 0, the iterator taken by `begin()` is always the
// same as that of `end()`, and accessing through `operator[]` or iterators
// always causes a segmentation fault. DO NOT access to the element of this.
//
// Why this is chose as the default type is because the last version (2.x.y)
// does not contain any comments in a value. To minimize the impact on the
// efficiency, this is chosen as a default.
//
// To reduce the memory footprint, later we can try empty base optimization (EBO).
struct discard_comments
{
    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;
    using value_type             = std::string;
    using reference              = std::string&;
    using const_reference        = std::string const&;
    using pointer                = std::string*;
    using const_pointer          = std::string const*;
    using iterator               = detail::empty_iterator<std::string, false>;
    using const_iterator         = detail::empty_iterator<std::string, true>;
    using reverse_iterator       = detail::empty_iterator<std::string, false>;
    using const_reverse_iterator = detail::empty_iterator<std::string, true>;

    discard_comments() = default;
    ~discard_comments() = default;
    discard_comments(discard_comments const&) = default;
    discard_comments(discard_comments &&)     = default;
    discard_comments& operator=(discard_comments const&) = default;
    discard_comments& operator=(discard_comments &&)     = default;

    explicit discard_comments(const std::vector<std::string>&) noexcept {}
    explicit discard_comments(std::vector<std::string>&&)      noexcept {}
    discard_comments& operator=(const std::vector<std::string>&) noexcept {return *this;}
    discard_comments& operator=(std::vector<std::string>&&)      noexcept {return *this;}

    explicit discard_comments(const preserve_comments&)        noexcept {}

    explicit discard_comments(size_type) noexcept {}
    discard_comments(size_type, const std::string&) noexcept {}
    discard_comments(std::initializer_list<std::string>) noexcept {}
    template<typename InputIterator>
    discard_comments(InputIterator, InputIterator) noexcept {}

    template<typename InputIterator>
    void assign(InputIterator, InputIterator)       noexcept {}
    void assign(std::initializer_list<std::string>) noexcept {}
    void assign(size_type, const std::string&)      noexcept {}

    iterator insert(const_iterator, const std::string&)                 {return iterator{};}
    iterator insert(const_iterator, std::string&&)                      {return iterator{};}
    iterator insert(const_iterator, size_type, const std::string&)      {return iterator{};}
    template<typename InputIterator>
    iterator insert(const_iterator, InputIterator, InputIterator)       {return iterator{};}
    iterator insert(const_iterator, std::initializer_list<std::string>) {return iterator{};}

    template<typename ... Ts>
    iterator emplace(const_iterator, Ts&& ...)     {return iterator{};}
    iterator erase(const_iterator)                 {return iterator{};}
    iterator erase(const_iterator, const_iterator) {return iterator{};}

    void swap(discard_comments&) {return;}

    void push_back(const std::string&) {return;}
    void push_back(std::string&&     ) {return;}
    void pop_back()                    {return;}

    template<typename ... Ts>
    void emplace_back(Ts&& ...) {return;}

    void clear() {return;}

    size_type size()     const noexcept {return 0;}
    size_type max_size() const noexcept {return 0;}
    size_type capacity() const noexcept {return 0;}
    bool      empty()    const noexcept {return true;}

    void reserve(size_type)                    {return;}
    void resize(size_type)                     {return;}
    void resize(size_type, const std::string&) {return;}
    void shrink_to_fit()                       {return;}

    // DO NOT access to the element of this container. This container is always
    // empty, so accessing through operator[], front/back, data causes address
    // error.

    reference       operator[](const size_type)       noexcept {never_call("toml::discard_comment::operator[]");}
    const_reference operator[](const size_type) const noexcept {never_call("toml::discard_comment::operator[]");}
    reference       at(const size_type)       {throw std::out_of_range("toml::discard_comment is always empty.");}
    const_reference at(const size_type) const {throw std::out_of_range("toml::discard_comment is always empty.");}
    reference       front()       noexcept {never_call("toml::discard_comment::front");}
    const_reference front() const noexcept {never_call("toml::discard_comment::front");}
    reference       back()        noexcept {never_call("toml::discard_comment::back");}
    const_reference back()  const noexcept {never_call("toml::discard_comment::back");}

    pointer         data()        noexcept {return nullptr;}
    const_pointer   data()  const noexcept {return nullptr;}

    iterator       begin()        noexcept {return iterator{};}
    iterator       end()          noexcept {return iterator{};}
    const_iterator begin()  const noexcept {return const_iterator{};}
    const_iterator end()    const noexcept {return const_iterator{};}
    const_iterator cbegin() const noexcept {return const_iterator{};}
    const_iterator cend()   const noexcept {return const_iterator{};}

    reverse_iterator       rbegin()        noexcept {return iterator{};}
    reverse_iterator       rend()          noexcept {return iterator{};}
    const_reverse_iterator rbegin()  const noexcept {return const_iterator{};}
    const_reverse_iterator rend()    const noexcept {return const_iterator{};}
    const_reverse_iterator crbegin() const noexcept {return const_iterator{};}
    const_reverse_iterator crend()   const noexcept {return const_iterator{};}

  private:

    [[noreturn]] static void never_call(const char *const this_function)
    {
#ifdef __has_builtin
#  if __has_builtin(__builtin_unreachable)
        __builtin_unreachable();
#  endif
#endif
        throw std::logic_error{this_function};
    }
};

inline bool operator==(const discard_comments&, const discard_comments&) noexcept {return true;}
inline bool operator!=(const discard_comments&, const discard_comments&) noexcept {return false;}
inline bool operator< (const discard_comments&, const discard_comments&) noexcept {return false;}
inline bool operator<=(const discard_comments&, const discard_comments&) noexcept {return true;}
inline bool operator> (const discard_comments&, const discard_comments&) noexcept {return false;}
inline bool operator>=(const discard_comments&, const discard_comments&) noexcept {return true;}

inline void swap(const discard_comments&, const discard_comments&) noexcept {return;}

template<typename charT, typename traits>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const discard_comments&)
{
    return os;
}

} // toml11
#endif// TOML11_COMMENTS_HPP
