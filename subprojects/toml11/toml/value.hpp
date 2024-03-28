//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_VALUE_HPP
#define TOML11_VALUE_HPP
#include <cassert>

#include "comments.hpp"
#include "exception.hpp"
#include "into.hpp"
#include "region.hpp"
#include "source_location.hpp"
#include "storage.hpp"
#include "traits.hpp"
#include "types.hpp"
#include "utility.hpp"

namespace toml
{

namespace detail
{

// to show error messages. not recommended for users.
template<typename Value>
inline region_base const* get_region(const Value& v)
{
    return v.region_info_.get();
}

template<typename Value>
void change_region(Value& v, region reg)
{
    v.region_info_ = std::make_shared<region>(std::move(reg));
    return;
}

template<value_t Expected, typename Value>
[[noreturn]] inline void
throw_bad_cast(const std::string& funcname, value_t actual, const Value& v)
{
    throw type_error(detail::format_underline(
        concat_to_string(funcname, "bad_cast to ", Expected), {
            {v.location(), concat_to_string("the actual type is ", actual)}
        }), v.location());
}

// Throw `out_of_range` from `toml::value::at()` and `toml::find()`
// after generating an error message.
//
// The implementation is a bit complicated and there are many edge-cases.
// If you are not interested in the error message generation, just skip this.
template<typename Value>
[[noreturn]] void
throw_key_not_found_error(const Value& v, const key& ky)
{
    // The top-level table has its region at the first character of the file.
    // That means that, in the case when a key is not found in the top-level
    // table, the error message points to the first character. If the file has
    // its first table at the first line, the error message would be like this.
    // ```console
    // [error] key "a" not found
    //  --> example.toml
    //    |
    //  1 | [table]
    //    | ^------ in this table
    // ```
    // It actually points to the top-level table at the first character,
    // not `[table]`. But it is too confusing. To avoid the confusion, the error
    // message should explicitly say "key not found in the top-level table",
    // or "the parsed file is empty" if there is no content at all (0 bytes in file).
    const auto loc = v.location();
    if(loc.line() == 1 && loc.region() == 0)
    {
        // First line with a zero-length region means "empty file".
        // The region will be generated at `parse_toml_file` function
        // if the file contains no bytes.
        throw std::out_of_range(format_underline(concat_to_string(
            "key \"", ky, "\" not found in the top-level table"), {
                {loc, "the parsed file is empty"}
            }));
    }
    else if(loc.line() == 1 && loc.region() == 1)
    {
        // Here it assumes that top-level table starts at the first character.
        // The region corresponds to the top-level table will be generated at
        // `parse_toml_file` function.
        //     It also assumes that the top-level table size is just one and
        // the line number is `1`. It is always satisfied. And those conditions
        // are satisfied only if the table is the top-level table.
        //
        // 1. one-character dot-key at the first line
        // ```toml
        // a.b = "c"
        // ```
        // toml11 counts whole key as the table key. Here, `a.b` is the region
        // of the table "a". It could be counter intuitive, but it works.
        // The size of the region is 3, not 1. The above example is the shortest
        // dot-key example. The size cannot be 1.
        //
        // 2. one-character inline-table at the first line
        // ```toml
        // a = {b = "c"}
        // ```
        // toml11 considers the inline table body as the table region. Here,
        // `{b = "c"}` is the region of the table "a". The size of the region
        // is 9, not 1. The shotest inline table still has two characters, `{`
        // and `}`. The size cannot be 1.
        //
        // 3. one-character table declaration at the first line
        // ```toml
        // [a]
        // ```
        // toml11 considers the whole table key as the table region. Here,
        // `[a]` is the table region. The size is 3, not 1.
        //
        throw std::out_of_range(format_underline(concat_to_string(
            "key \"", ky, "\" not found in the top-level table"), {
                {loc, "the top-level table starts here"}
            }));
    }
    else
    {
        // normal table.
        throw std::out_of_range(format_underline(concat_to_string(
            "key \"", ky, "\" not found"), { {loc, "in this table"} }));
    }
}

// switch by `value_t` at the compile time.
template<value_t T>
struct switch_cast {};
#define TOML11_GENERATE_SWITCH_CASTER(TYPE) \
    template<>                                                           \
    struct switch_cast<value_t::TYPE>                                    \
    {                                                                    \
        template<typename Value>                                         \
        static typename Value::TYPE##_type& invoke(Value& v)             \
        {                                                                \
            return v.as_##TYPE();                                        \
        }                                                                \
        template<typename Value>                                         \
        static typename Value::TYPE##_type const& invoke(const Value& v) \
        {                                                                \
            return v.as_##TYPE();                                        \
        }                                                                \
        template<typename Value>                                         \
        static typename Value::TYPE##_type&& invoke(Value&& v)           \
        {                                                                \
            return std::move(v).as_##TYPE();                             \
        }                                                                \
    };                                                                   \
    /**/
TOML11_GENERATE_SWITCH_CASTER(boolean)
TOML11_GENERATE_SWITCH_CASTER(integer)
TOML11_GENERATE_SWITCH_CASTER(floating)
TOML11_GENERATE_SWITCH_CASTER(string)
TOML11_GENERATE_SWITCH_CASTER(offset_datetime)
TOML11_GENERATE_SWITCH_CASTER(local_datetime)
TOML11_GENERATE_SWITCH_CASTER(local_date)
TOML11_GENERATE_SWITCH_CASTER(local_time)
TOML11_GENERATE_SWITCH_CASTER(array)
TOML11_GENERATE_SWITCH_CASTER(table)

#undef TOML11_GENERATE_SWITCH_CASTER

}// detail

template<typename Comment, // discard/preserve_comment
         template<typename ...> class Table = std::unordered_map,
         template<typename ...> class Array = std::vector>
class basic_value
{
    template<typename T, typename U>
    static void assigner(T& dst, U&& v)
    {
        const auto tmp = ::new(std::addressof(dst)) T(std::forward<U>(v));
        assert(tmp == std::addressof(dst));
        (void)tmp;
    }

    using region_base = detail::region_base;

    template<typename C, template<typename ...> class T,
             template<typename ...> class A>
    friend class basic_value;

  public:

    using comment_type         = Comment;
    using key_type             = ::toml::key;
    using value_type           = basic_value<comment_type, Table, Array>;
    using boolean_type         = ::toml::boolean;
    using integer_type         = ::toml::integer;
    using floating_type        = ::toml::floating;
    using string_type          = ::toml::string;
    using local_time_type      = ::toml::local_time;
    using local_date_type      = ::toml::local_date;
    using local_datetime_type  = ::toml::local_datetime;
    using offset_datetime_type = ::toml::offset_datetime;
    using array_type           = Array<value_type>;
    using table_type           = Table<key_type, value_type>;

  public:

    basic_value() noexcept
        : type_(value_t::empty),
          region_info_(std::make_shared<region_base>(region_base{}))
    {}
    ~basic_value() noexcept {this->cleanup();}

    basic_value(const basic_value& v)
        : type_(v.type()), region_info_(v.region_info_), comments_(v.comments_)
    {
        switch(v.type())
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          : assigner(array_          , v.array_          ); break;
            case value_t::table          : assigner(table_          , v.table_          ); break;
            default: break;
        }
    }
    basic_value(basic_value&& v)
        : type_(v.type()), region_info_(std::move(v.region_info_)),
          comments_(std::move(v.comments_))
    {
        switch(this->type_) // here this->type_ is already initialized
        {
            case value_t::boolean        : assigner(boolean_        , std::move(v.boolean_        )); break;
            case value_t::integer        : assigner(integer_        , std::move(v.integer_        )); break;
            case value_t::floating       : assigner(floating_       , std::move(v.floating_       )); break;
            case value_t::string         : assigner(string_         , std::move(v.string_         )); break;
            case value_t::offset_datetime: assigner(offset_datetime_, std::move(v.offset_datetime_)); break;
            case value_t::local_datetime : assigner(local_datetime_ , std::move(v.local_datetime_ )); break;
            case value_t::local_date     : assigner(local_date_     , std::move(v.local_date_     )); break;
            case value_t::local_time     : assigner(local_time_     , std::move(v.local_time_     )); break;
            case value_t::array          : assigner(array_          , std::move(v.array_          )); break;
            case value_t::table          : assigner(table_          , std::move(v.table_          )); break;
            default: break;
        }
    }
    basic_value& operator=(const basic_value& v)
    {
        if(this == std::addressof(v)) {return *this;}
        this->cleanup();
        this->region_info_ = v.region_info_;
        this->comments_ = v.comments_;
        this->type_ = v.type();
        switch(this->type_)
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          : assigner(array_          , v.array_          ); break;
            case value_t::table          : assigner(table_          , v.table_          ); break;
            default: break;
        }
        return *this;
    }
    basic_value& operator=(basic_value&& v)
    {
        if(this == std::addressof(v)) {return *this;}
        this->cleanup();
        this->region_info_ = std::move(v.region_info_);
        this->comments_ = std::move(v.comments_);
        this->type_ = v.type();
        switch(this->type_)
        {
            case value_t::boolean        : assigner(boolean_        , std::move(v.boolean_        )); break;
            case value_t::integer        : assigner(integer_        , std::move(v.integer_        )); break;
            case value_t::floating       : assigner(floating_       , std::move(v.floating_       )); break;
            case value_t::string         : assigner(string_         , std::move(v.string_         )); break;
            case value_t::offset_datetime: assigner(offset_datetime_, std::move(v.offset_datetime_)); break;
            case value_t::local_datetime : assigner(local_datetime_ , std::move(v.local_datetime_ )); break;
            case value_t::local_date     : assigner(local_date_     , std::move(v.local_date_     )); break;
            case value_t::local_time     : assigner(local_time_     , std::move(v.local_time_     )); break;
            case value_t::array          : assigner(array_          , std::move(v.array_          )); break;
            case value_t::table          : assigner(table_          , std::move(v.table_          )); break;
            default: break;
        }
        return *this;
    }

    // overwrite comments ----------------------------------------------------

    basic_value(const basic_value& v, std::vector<std::string> com)
        : type_(v.type()), region_info_(v.region_info_),
          comments_(std::move(com))
    {
        switch(v.type())
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          : assigner(array_          , v.array_          ); break;
            case value_t::table          : assigner(table_          , v.table_          ); break;
            default: break;
        }
    }

    basic_value(basic_value&& v, std::vector<std::string> com)
        : type_(v.type()), region_info_(std::move(v.region_info_)),
          comments_(std::move(com))
    {
        switch(this->type_) // here this->type_ is already initialized
        {
            case value_t::boolean        : assigner(boolean_        , std::move(v.boolean_        )); break;
            case value_t::integer        : assigner(integer_        , std::move(v.integer_        )); break;
            case value_t::floating       : assigner(floating_       , std::move(v.floating_       )); break;
            case value_t::string         : assigner(string_         , std::move(v.string_         )); break;
            case value_t::offset_datetime: assigner(offset_datetime_, std::move(v.offset_datetime_)); break;
            case value_t::local_datetime : assigner(local_datetime_ , std::move(v.local_datetime_ )); break;
            case value_t::local_date     : assigner(local_date_     , std::move(v.local_date_     )); break;
            case value_t::local_time     : assigner(local_time_     , std::move(v.local_time_     )); break;
            case value_t::array          : assigner(array_          , std::move(v.array_          )); break;
            case value_t::table          : assigner(table_          , std::move(v.table_          )); break;
            default: break;
        }
    }

    // -----------------------------------------------------------------------
    // conversion between different basic_values.
    template<typename C,
             template<typename ...> class T,
             template<typename ...> class A>
    basic_value(const basic_value<C, T, A>& v)
        : type_(v.type()), region_info_(v.region_info_), comments_(v.comments())
    {
        switch(v.type())
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          :
            {
                array_type tmp(v.as_array(std::nothrow).begin(),
                               v.as_array(std::nothrow).end());
                assigner(array_, std::move(tmp));
                break;
            }
            case value_t::table          :
            {
                table_type tmp(v.as_table(std::nothrow).begin(),
                               v.as_table(std::nothrow).end());
                assigner(table_, std::move(tmp));
                break;
            }
            default: break;
        }
    }
    template<typename C,
             template<typename ...> class T,
             template<typename ...> class A>
    basic_value(const basic_value<C, T, A>& v, std::vector<std::string> com)
        : type_(v.type()), region_info_(v.region_info_),
          comments_(std::move(com))
    {
        switch(v.type())
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          :
            {
                array_type tmp(v.as_array(std::nothrow).begin(),
                               v.as_array(std::nothrow).end());
                assigner(array_, std::move(tmp));
                break;
            }
            case value_t::table          :
            {
                table_type tmp(v.as_table(std::nothrow).begin(),
                               v.as_table(std::nothrow).end());
                assigner(table_, std::move(tmp));
                break;
            }
            default: break;
        }
    }
    template<typename C,
             template<typename ...> class T,
             template<typename ...> class A>
    basic_value& operator=(const basic_value<C, T, A>& v)
    {
        this->region_info_ = v.region_info_;
        this->comments_    = comment_type(v.comments());
        this->type_        = v.type();
        switch(v.type())
        {
            case value_t::boolean        : assigner(boolean_        , v.boolean_        ); break;
            case value_t::integer        : assigner(integer_        , v.integer_        ); break;
            case value_t::floating       : assigner(floating_       , v.floating_       ); break;
            case value_t::string         : assigner(string_         , v.string_         ); break;
            case value_t::offset_datetime: assigner(offset_datetime_, v.offset_datetime_); break;
            case value_t::local_datetime : assigner(local_datetime_ , v.local_datetime_ ); break;
            case value_t::local_date     : assigner(local_date_     , v.local_date_     ); break;
            case value_t::local_time     : assigner(local_time_     , v.local_time_     ); break;
            case value_t::array          :
            {
                array_type tmp(v.as_array(std::nothrow).begin(),
                               v.as_array(std::nothrow).end());
                assigner(array_, std::move(tmp));
                break;
            }
            case value_t::table          :
            {
                table_type tmp(v.as_table(std::nothrow).begin(),
                               v.as_table(std::nothrow).end());
                assigner(table_, std::move(tmp));
                break;
            }
            default: break;
        }
        return *this;
    }

    // boolean ==============================================================

    basic_value(boolean b)
        : type_(value_t::boolean),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->boolean_, b);
    }
    basic_value& operator=(boolean b)
    {
        this->cleanup();
        this->type_ = value_t::boolean;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->boolean_, b);
        return *this;
    }
    basic_value(boolean b, std::vector<std::string> com)
        : type_(value_t::boolean),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->boolean_, b);
    }

    // integer ==============================================================

    template<typename T, typename std::enable_if<detail::conjunction<
        std::is_integral<T>, detail::negation<std::is_same<T, boolean>>>::value,
        std::nullptr_t>::type = nullptr>
    basic_value(T i)
        : type_(value_t::integer),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->integer_, static_cast<integer>(i));
    }

    template<typename T, typename std::enable_if<detail::conjunction<
        std::is_integral<T>, detail::negation<std::is_same<T, boolean>>>::value,
        std::nullptr_t>::type = nullptr>
    basic_value& operator=(T i)
    {
        this->cleanup();
        this->type_ = value_t::integer;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->integer_, static_cast<integer>(i));
        return *this;
    }

    template<typename T, typename std::enable_if<detail::conjunction<
        std::is_integral<T>, detail::negation<std::is_same<T, boolean>>>::value,
        std::nullptr_t>::type = nullptr>
    basic_value(T i, std::vector<std::string> com)
        : type_(value_t::integer),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->integer_, static_cast<integer>(i));
    }

    // floating =============================================================

    template<typename T, typename std::enable_if<
        std::is_floating_point<T>::value, std::nullptr_t>::type = nullptr>
    basic_value(T f)
        : type_(value_t::floating),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->floating_, static_cast<floating>(f));
    }


    template<typename T, typename std::enable_if<
        std::is_floating_point<T>::value, std::nullptr_t>::type = nullptr>
    basic_value& operator=(T f)
    {
        this->cleanup();
        this->type_ = value_t::floating;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->floating_, static_cast<floating>(f));
        return *this;
    }

    template<typename T, typename std::enable_if<
        std::is_floating_point<T>::value, std::nullptr_t>::type = nullptr>
    basic_value(T f, std::vector<std::string> com)
        : type_(value_t::floating),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->floating_, f);
    }

    // string ===============================================================

    basic_value(toml::string s)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, std::move(s));
    }
    basic_value& operator=(toml::string s)
    {
        this->cleanup();
        this->type_ = value_t::string ;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->string_, s);
        return *this;
    }
    basic_value(toml::string s, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, std::move(s));
    }

    basic_value(std::string s)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(std::move(s)));
    }
    basic_value& operator=(std::string s)
    {
        this->cleanup();
        this->type_ = value_t::string ;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->string_, toml::string(std::move(s)));
        return *this;
    }
    basic_value(std::string s, string_t kind)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(std::move(s), kind));
    }
    basic_value(std::string s, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(std::move(s)));
    }
    basic_value(std::string s, string_t kind, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(std::move(s), kind));
    }

    basic_value(const char* s)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(std::string(s)));
    }
    basic_value& operator=(const char* s)
    {
        this->cleanup();
        this->type_ = value_t::string ;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->string_, toml::string(std::string(s)));
        return *this;
    }
    basic_value(const char* s, string_t kind)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(std::string(s), kind));
    }
    basic_value(const char* s, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(std::string(s)));
    }
    basic_value(const char* s, string_t kind, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(std::string(s), kind));
    }

#if defined(TOML11_USING_STRING_VIEW) && TOML11_USING_STRING_VIEW>0
    basic_value(std::string_view s)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(s));
    }
    basic_value& operator=(std::string_view s)
    {
        this->cleanup();
        this->type_ = value_t::string ;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->string_, toml::string(s));
        return *this;
    }
    basic_value(std::string_view s, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(s));
    }
    basic_value(std::string_view s, string_t kind)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->string_, toml::string(s, kind));
    }
    basic_value(std::string_view s, string_t kind, std::vector<std::string> com)
        : type_(value_t::string),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->string_, toml::string(s, kind));
    }
#endif

    // local date ===========================================================

    basic_value(const local_date& ld)
        : type_(value_t::local_date),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->local_date_, ld);
    }
    basic_value& operator=(const local_date& ld)
    {
        this->cleanup();
        this->type_ = value_t::local_date;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->local_date_, ld);
        return *this;
    }
    basic_value(const local_date& ld, std::vector<std::string> com)
        : type_(value_t::local_date),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->local_date_, ld);
    }

    // local time ===========================================================

    basic_value(const local_time& lt)
        : type_(value_t::local_time),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->local_time_, lt);
    }
    basic_value(const local_time& lt, std::vector<std::string> com)
        : type_(value_t::local_time),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->local_time_, lt);
    }
    basic_value& operator=(const local_time& lt)
    {
        this->cleanup();
        this->type_ = value_t::local_time;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->local_time_, lt);
        return *this;
    }

    template<typename Rep, typename Period>
    basic_value(const std::chrono::duration<Rep, Period>& dur)
        : type_(value_t::local_time),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->local_time_, local_time(dur));
    }
    template<typename Rep, typename Period>
    basic_value(const std::chrono::duration<Rep, Period>& dur,
                std::vector<std::string> com)
        : type_(value_t::local_time),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->local_time_, local_time(dur));
    }
    template<typename Rep, typename Period>
    basic_value& operator=(const std::chrono::duration<Rep, Period>& dur)
    {
        this->cleanup();
        this->type_ = value_t::local_time;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->local_time_, local_time(dur));
        return *this;
    }

    // local datetime =======================================================

    basic_value(const local_datetime& ldt)
        : type_(value_t::local_datetime),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->local_datetime_, ldt);
    }
    basic_value(const local_datetime& ldt, std::vector<std::string> com)
        : type_(value_t::local_datetime),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->local_datetime_, ldt);
    }
    basic_value& operator=(const local_datetime& ldt)
    {
        this->cleanup();
        this->type_ = value_t::local_datetime;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->local_datetime_, ldt);
        return *this;
    }

    // offset datetime ======================================================

    basic_value(const offset_datetime& odt)
        : type_(value_t::offset_datetime),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->offset_datetime_, odt);
    }
    basic_value(const offset_datetime& odt, std::vector<std::string> com)
        : type_(value_t::offset_datetime),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->offset_datetime_, odt);
    }
    basic_value& operator=(const offset_datetime& odt)
    {
        this->cleanup();
        this->type_ = value_t::offset_datetime;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->offset_datetime_, odt);
        return *this;
    }
    basic_value(const std::chrono::system_clock::time_point& tp)
        : type_(value_t::offset_datetime),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->offset_datetime_, offset_datetime(tp));
    }
    basic_value(const std::chrono::system_clock::time_point& tp,
                std::vector<std::string> com)
        : type_(value_t::offset_datetime),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->offset_datetime_, offset_datetime(tp));
    }
    basic_value& operator=(const std::chrono::system_clock::time_point& tp)
    {
        this->cleanup();
        this->type_ = value_t::offset_datetime;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->offset_datetime_, offset_datetime(tp));
        return *this;
    }

    // array ================================================================

    basic_value(const array_type& ary)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->array_, ary);
    }
    basic_value(const array_type& ary, std::vector<std::string> com)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->array_, ary);
    }
    basic_value& operator=(const array_type& ary)
    {
        this->cleanup();
        this->type_ = value_t::array ;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->array_, ary);
        return *this;
    }

    // array (initializer_list) ----------------------------------------------

    template<typename T, typename std::enable_if<
            std::is_convertible<T, value_type>::value,
        std::nullptr_t>::type = nullptr>
    basic_value(std::initializer_list<T> list)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        array_type ary(list.begin(), list.end());
        assigner(this->array_, std::move(ary));
    }
    template<typename T, typename std::enable_if<
            std::is_convertible<T, value_type>::value,
        std::nullptr_t>::type = nullptr>
    basic_value(std::initializer_list<T> list, std::vector<std::string> com)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        array_type ary(list.begin(), list.end());
        assigner(this->array_, std::move(ary));
    }
    template<typename T, typename std::enable_if<
            std::is_convertible<T, value_type>::value,
        std::nullptr_t>::type = nullptr>
    basic_value& operator=(std::initializer_list<T> list)
    {
        this->cleanup();
        this->type_ = value_t::array;
        this->region_info_ = std::make_shared<region_base>(region_base{});

        array_type ary(list.begin(), list.end());
        assigner(this->array_, std::move(ary));
        return *this;
    }

    // array (STL Containers) ------------------------------------------------

    template<typename T, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<T, array_type>>,
            detail::is_container<T>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value(const T& list)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        static_assert(std::is_convertible<typename T::value_type, value_type>::value,
            "elements of a container should be convertible to toml::value");

        array_type ary(list.size());
        std::copy(list.begin(), list.end(), ary.begin());
        assigner(this->array_, std::move(ary));
    }
    template<typename T, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<T, array_type>>,
            detail::is_container<T>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value(const T& list, std::vector<std::string> com)
        : type_(value_t::array),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        static_assert(std::is_convertible<typename T::value_type, value_type>::value,
            "elements of a container should be convertible to toml::value");

        array_type ary(list.size());
        std::copy(list.begin(), list.end(), ary.begin());
        assigner(this->array_, std::move(ary));
    }
    template<typename T, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<T, array_type>>,
            detail::is_container<T>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value& operator=(const T& list)
    {
        static_assert(std::is_convertible<typename T::value_type, value_type>::value,
            "elements of a container should be convertible to toml::value");

        this->cleanup();
        this->type_ = value_t::array;
        this->region_info_ = std::make_shared<region_base>(region_base{});

        array_type ary(list.size());
        std::copy(list.begin(), list.end(), ary.begin());
        assigner(this->array_, std::move(ary));
        return *this;
    }

    // table ================================================================

    basic_value(const table_type& tab)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        assigner(this->table_, tab);
    }
    basic_value(const table_type& tab, std::vector<std::string> com)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        assigner(this->table_, tab);
    }
    basic_value& operator=(const table_type& tab)
    {
        this->cleanup();
        this->type_ = value_t::table;
        this->region_info_ = std::make_shared<region_base>(region_base{});
        assigner(this->table_, tab);
        return *this;
    }

    // initializer-list ------------------------------------------------------

    basic_value(std::initializer_list<std::pair<key, basic_value>> list)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        table_type tab;
        for(const auto& elem : list) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
    }

    basic_value(std::initializer_list<std::pair<key, basic_value>> list,
                std::vector<std::string> com)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        table_type tab;
        for(const auto& elem : list) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
    }
    basic_value& operator=(std::initializer_list<std::pair<key, basic_value>> list)
    {
        this->cleanup();
        this->type_ = value_t::table;
        this->region_info_ = std::make_shared<region_base>(region_base{});

        table_type tab;
        for(const auto& elem : list) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
        return *this;
    }

    // other table-like -----------------------------------------------------

    template<typename Map, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<Map, table_type>>,
            detail::is_map<Map>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value(const Map& mp)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{}))
    {
        table_type tab;
        for(const auto& elem : mp) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
    }
    template<typename Map, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<Map, table_type>>,
            detail::is_map<Map>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value(const Map& mp, std::vector<std::string> com)
        : type_(value_t::table),
          region_info_(std::make_shared<region_base>(region_base{})),
          comments_(std::move(com))
    {
        table_type tab;
        for(const auto& elem : mp) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
    }
    template<typename Map, typename std::enable_if<detail::conjunction<
            detail::negation<std::is_same<Map, table_type>>,
            detail::is_map<Map>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value& operator=(const Map& mp)
    {
        this->cleanup();
        this->type_ = value_t::table;
        this->region_info_ = std::make_shared<region_base>(region_base{});

        table_type tab;
        for(const auto& elem : mp) {tab[elem.first] = elem.second;}
        assigner(this->table_, std::move(tab));
        return *this;
    }

    // user-defined =========================================================

    // convert using into_toml() method -------------------------------------

    template<typename T, typename std::enable_if<
        detail::has_into_toml_method<T>::value, std::nullptr_t>::type = nullptr>
    basic_value(const T& ud): basic_value(ud.into_toml()) {}

    template<typename T, typename std::enable_if<
        detail::has_into_toml_method<T>::value, std::nullptr_t>::type = nullptr>
    basic_value(const T& ud, std::vector<std::string> com)
        : basic_value(ud.into_toml(), std::move(com))
    {}
    template<typename T, typename std::enable_if<
        detail::has_into_toml_method<T>::value, std::nullptr_t>::type = nullptr>
    basic_value& operator=(const T& ud)
    {
        *this = ud.into_toml();
        return *this;
    }

    // convert using into<T> struct -----------------------------------------

    template<typename T, std::size_t S = sizeof(::toml::into<T>)>
    basic_value(const T& ud): basic_value(::toml::into<T>::into_toml(ud)) {}
    template<typename T, std::size_t S = sizeof(::toml::into<T>)>
    basic_value(const T& ud, std::vector<std::string> com)
        : basic_value(::toml::into<T>::into_toml(ud), std::move(com))
    {}
    template<typename T, std::size_t S = sizeof(::toml::into<T>)>
    basic_value& operator=(const T& ud)
    {
        *this = ::toml::into<T>::into_toml(ud);
        return *this;
    }

    // for internal use ------------------------------------------------------
    //
    // Those constructors take detail::region that contains parse result.

    basic_value(boolean b, detail::region reg, std::vector<std::string> cm)
        : type_(value_t::boolean),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->boolean_, b);
    }
    template<typename T, typename std::enable_if<
        detail::conjunction<
            std::is_integral<T>, detail::negation<std::is_same<T, boolean>>
        >::value, std::nullptr_t>::type = nullptr>
    basic_value(T i, detail::region reg, std::vector<std::string> cm)
        : type_(value_t::integer),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->integer_, static_cast<integer>(i));
    }
    template<typename T, typename std::enable_if<
        std::is_floating_point<T>::value, std::nullptr_t>::type = nullptr>
    basic_value(T f, detail::region reg, std::vector<std::string> cm)
        : type_(value_t::floating),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->floating_, static_cast<floating>(f));
    }
    basic_value(toml::string s, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::string),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->string_, std::move(s));
    }
    basic_value(const local_date& ld, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::local_date),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->local_date_, ld);
    }
    basic_value(const local_time& lt, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::local_time),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->local_time_, lt);
    }
    basic_value(const local_datetime& ldt, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::local_datetime),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->local_datetime_, ldt);
    }
    basic_value(const offset_datetime& odt, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::offset_datetime),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->offset_datetime_, odt);
    }
    basic_value(const array_type& ary, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::array),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->array_, ary);
    }
    basic_value(const table_type& tab, detail::region reg,
                std::vector<std::string> cm)
        : type_(value_t::table),
          region_info_(std::make_shared<detail::region>(std::move(reg))),
          comments_(std::move(cm))
    {
        assigner(this->table_, tab);
    }

    template<typename T, typename std::enable_if<
        detail::is_exact_toml_type<T, value_type>::value,
        std::nullptr_t>::type = nullptr>
    basic_value(std::pair<T, detail::region> parse_result, std::vector<std::string> com)
        : basic_value(std::move(parse_result.first),
                      std::move(parse_result.second),
                      std::move(com))
    {}

    // type checking and casting ============================================

    template<typename T, typename std::enable_if<
        detail::is_exact_toml_type<T, value_type>::value,
        std::nullptr_t>::type = nullptr>
    bool is() const noexcept
    {
        return detail::type_to_enum<T, value_type>::value == this->type_;
    }
    bool is(value_t t) const noexcept {return t == this->type_;}

    bool is_uninitialized()   const noexcept {return this->is(value_t::empty          );}
    bool is_boolean()         const noexcept {return this->is(value_t::boolean        );}
    bool is_integer()         const noexcept {return this->is(value_t::integer        );}
    bool is_floating()        const noexcept {return this->is(value_t::floating       );}
    bool is_string()          const noexcept {return this->is(value_t::string         );}
    bool is_offset_datetime() const noexcept {return this->is(value_t::offset_datetime);}
    bool is_local_datetime()  const noexcept {return this->is(value_t::local_datetime );}
    bool is_local_date()      const noexcept {return this->is(value_t::local_date     );}
    bool is_local_time()      const noexcept {return this->is(value_t::local_time     );}
    bool is_array()           const noexcept {return this->is(value_t::array          );}
    bool is_table()           const noexcept {return this->is(value_t::table          );}

    value_t type() const noexcept {return type_;}

    template<value_t T>
    typename detail::enum_to_type<T, value_type>::type&       cast() &
    {
        if(this->type_ != T)
        {
            detail::throw_bad_cast<T>("toml::value::cast: ", this->type_, *this);
        }
        return detail::switch_cast<T>::invoke(*this);
    }
    template<value_t T>
    typename detail::enum_to_type<T, value_type>::type const& cast() const&
    {
        if(this->type_ != T)
        {
            detail::throw_bad_cast<T>("toml::value::cast: ", this->type_, *this);
        }
        return detail::switch_cast<T>::invoke(*this);
    }
    template<value_t T>
    typename detail::enum_to_type<T, value_type>::type&&      cast() &&
    {
        if(this->type_ != T)
        {
            detail::throw_bad_cast<T>("toml::value::cast: ", this->type_, *this);
        }
        return detail::switch_cast<T>::invoke(std::move(*this));
    }

    // ------------------------------------------------------------------------
    // nothrow version

    boolean         const& as_boolean        (const std::nothrow_t&) const& noexcept {return this->boolean_;}
    integer         const& as_integer        (const std::nothrow_t&) const& noexcept {return this->integer_;}
    floating        const& as_floating       (const std::nothrow_t&) const& noexcept {return this->floating_;}
    string          const& as_string         (const std::nothrow_t&) const& noexcept {return this->string_;}
    offset_datetime const& as_offset_datetime(const std::nothrow_t&) const& noexcept {return this->offset_datetime_;}
    local_datetime  const& as_local_datetime (const std::nothrow_t&) const& noexcept {return this->local_datetime_;}
    local_date      const& as_local_date     (const std::nothrow_t&) const& noexcept {return this->local_date_;}
    local_time      const& as_local_time     (const std::nothrow_t&) const& noexcept {return this->local_time_;}
    array_type      const& as_array          (const std::nothrow_t&) const& noexcept {return this->array_.value();}
    table_type      const& as_table          (const std::nothrow_t&) const& noexcept {return this->table_.value();}

    boolean        & as_boolean        (const std::nothrow_t&) & noexcept {return this->boolean_;}
    integer        & as_integer        (const std::nothrow_t&) & noexcept {return this->integer_;}
    floating       & as_floating       (const std::nothrow_t&) & noexcept {return this->floating_;}
    string         & as_string         (const std::nothrow_t&) & noexcept {return this->string_;}
    offset_datetime& as_offset_datetime(const std::nothrow_t&) & noexcept {return this->offset_datetime_;}
    local_datetime & as_local_datetime (const std::nothrow_t&) & noexcept {return this->local_datetime_;}
    local_date     & as_local_date     (const std::nothrow_t&) & noexcept {return this->local_date_;}
    local_time     & as_local_time     (const std::nothrow_t&) & noexcept {return this->local_time_;}
    array_type     & as_array          (const std::nothrow_t&) & noexcept {return this->array_.value();}
    table_type     & as_table          (const std::nothrow_t&) & noexcept {return this->table_.value();}

    boolean        && as_boolean        (const std::nothrow_t&) && noexcept {return std::move(this->boolean_);}
    integer        && as_integer        (const std::nothrow_t&) && noexcept {return std::move(this->integer_);}
    floating       && as_floating       (const std::nothrow_t&) && noexcept {return std::move(this->floating_);}
    string         && as_string         (const std::nothrow_t&) && noexcept {return std::move(this->string_);}
    offset_datetime&& as_offset_datetime(const std::nothrow_t&) && noexcept {return std::move(this->offset_datetime_);}
    local_datetime && as_local_datetime (const std::nothrow_t&) && noexcept {return std::move(this->local_datetime_);}
    local_date     && as_local_date     (const std::nothrow_t&) && noexcept {return std::move(this->local_date_);}
    local_time     && as_local_time     (const std::nothrow_t&) && noexcept {return std::move(this->local_time_);}
    array_type     && as_array          (const std::nothrow_t&) && noexcept {return std::move(this->array_.value());}
    table_type     && as_table          (const std::nothrow_t&) && noexcept {return std::move(this->table_.value());}

    // ========================================================================
    // throw version
    // ------------------------------------------------------------------------
    // const reference {{{

    boolean const& as_boolean() const&
    {
        if(this->type_ != value_t::boolean)
        {
            detail::throw_bad_cast<value_t::boolean>(
                    "toml::value::as_boolean(): ", this->type_, *this);
        }
        return this->boolean_;
    }
    integer const& as_integer() const&
    {
        if(this->type_ != value_t::integer)
        {
            detail::throw_bad_cast<value_t::integer>(
                    "toml::value::as_integer(): ", this->type_, *this);
        }
        return this->integer_;
    }
    floating const& as_floating() const&
    {
        if(this->type_ != value_t::floating)
        {
            detail::throw_bad_cast<value_t::floating>(
                    "toml::value::as_floating(): ", this->type_, *this);
        }
        return this->floating_;
    }
    string const& as_string() const&
    {
        if(this->type_ != value_t::string)
        {
            detail::throw_bad_cast<value_t::string>(
                    "toml::value::as_string(): ", this->type_, *this);
        }
        return this->string_;
    }
    offset_datetime const& as_offset_datetime() const&
    {
        if(this->type_ != value_t::offset_datetime)
        {
            detail::throw_bad_cast<value_t::offset_datetime>(
                    "toml::value::as_offset_datetime(): ", this->type_, *this);
        }
        return this->offset_datetime_;
    }
    local_datetime const& as_local_datetime() const&
    {
        if(this->type_ != value_t::local_datetime)
        {
            detail::throw_bad_cast<value_t::local_datetime>(
                    "toml::value::as_local_datetime(): ", this->type_, *this);
        }
        return this->local_datetime_;
    }
    local_date const& as_local_date() const&
    {
        if(this->type_ != value_t::local_date)
        {
            detail::throw_bad_cast<value_t::local_date>(
                    "toml::value::as_local_date(): ", this->type_, *this);
        }
        return this->local_date_;
    }
    local_time const& as_local_time() const&
    {
        if(this->type_ != value_t::local_time)
        {
            detail::throw_bad_cast<value_t::local_time>(
                    "toml::value::as_local_time(): ", this->type_, *this);
        }
        return this->local_time_;
    }
    array_type const& as_array() const&
    {
        if(this->type_ != value_t::array)
        {
            detail::throw_bad_cast<value_t::array>(
                    "toml::value::as_array(): ", this->type_, *this);
        }
        return this->array_.value();
    }
    table_type const& as_table() const&
    {
        if(this->type_ != value_t::table)
        {
            detail::throw_bad_cast<value_t::table>(
                    "toml::value::as_table(): ", this->type_, *this);
        }
        return this->table_.value();
    }
    // }}}
    // ------------------------------------------------------------------------
    // nonconst reference {{{

    boolean & as_boolean() &
    {
        if(this->type_ != value_t::boolean)
        {
            detail::throw_bad_cast<value_t::boolean>(
                    "toml::value::as_boolean(): ", this->type_, *this);
        }
        return this->boolean_;
    }
    integer & as_integer() &
    {
        if(this->type_ != value_t::integer)
        {
            detail::throw_bad_cast<value_t::integer>(
                    "toml::value::as_integer(): ", this->type_, *this);
        }
        return this->integer_;
    }
    floating & as_floating() &
    {
        if(this->type_ != value_t::floating)
        {
            detail::throw_bad_cast<value_t::floating>(
                    "toml::value::as_floating(): ", this->type_, *this);
        }
        return this->floating_;
    }
    string & as_string() &
    {
        if(this->type_ != value_t::string)
        {
            detail::throw_bad_cast<value_t::string>(
                    "toml::value::as_string(): ", this->type_, *this);
        }
        return this->string_;
    }
    offset_datetime & as_offset_datetime() &
    {
        if(this->type_ != value_t::offset_datetime)
        {
            detail::throw_bad_cast<value_t::offset_datetime>(
                    "toml::value::as_offset_datetime(): ", this->type_, *this);
        }
        return this->offset_datetime_;
    }
    local_datetime & as_local_datetime() &
    {
        if(this->type_ != value_t::local_datetime)
        {
            detail::throw_bad_cast<value_t::local_datetime>(
                    "toml::value::as_local_datetime(): ", this->type_, *this);
        }
        return this->local_datetime_;
    }
    local_date & as_local_date() &
    {
        if(this->type_ != value_t::local_date)
        {
            detail::throw_bad_cast<value_t::local_date>(
                    "toml::value::as_local_date(): ", this->type_, *this);
        }
        return this->local_date_;
    }
    local_time & as_local_time() &
    {
        if(this->type_ != value_t::local_time)
        {
            detail::throw_bad_cast<value_t::local_time>(
                    "toml::value::as_local_time(): ", this->type_, *this);
        }
        return this->local_time_;
    }
    array_type & as_array() &
    {
        if(this->type_ != value_t::array)
        {
            detail::throw_bad_cast<value_t::array>(
                    "toml::value::as_array(): ", this->type_, *this);
        }
        return this->array_.value();
    }
    table_type & as_table() &
    {
        if(this->type_ != value_t::table)
        {
            detail::throw_bad_cast<value_t::table>(
                    "toml::value::as_table(): ", this->type_, *this);
        }
        return this->table_.value();
    }

    // }}}
    // ------------------------------------------------------------------------
    // rvalue reference {{{

    boolean && as_boolean() &&
    {
        if(this->type_ != value_t::boolean)
        {
            detail::throw_bad_cast<value_t::boolean>(
                    "toml::value::as_boolean(): ", this->type_, *this);
        }
        return std::move(this->boolean_);
    }
    integer && as_integer() &&
    {
        if(this->type_ != value_t::integer)
        {
            detail::throw_bad_cast<value_t::integer>(
                    "toml::value::as_integer(): ", this->type_, *this);
        }
        return std::move(this->integer_);
    }
    floating && as_floating() &&
    {
        if(this->type_ != value_t::floating)
        {
            detail::throw_bad_cast<value_t::floating>(
                    "toml::value::as_floating(): ", this->type_, *this);
        }
        return std::move(this->floating_);
    }
    string && as_string() &&
    {
        if(this->type_ != value_t::string)
        {
            detail::throw_bad_cast<value_t::string>(
                    "toml::value::as_string(): ", this->type_, *this);
        }
        return std::move(this->string_);
    }
    offset_datetime && as_offset_datetime() &&
    {
        if(this->type_ != value_t::offset_datetime)
        {
            detail::throw_bad_cast<value_t::offset_datetime>(
                    "toml::value::as_offset_datetime(): ", this->type_, *this);
        }
        return std::move(this->offset_datetime_);
    }
    local_datetime && as_local_datetime() &&
    {
        if(this->type_ != value_t::local_datetime)
        {
            detail::throw_bad_cast<value_t::local_datetime>(
                    "toml::value::as_local_datetime(): ", this->type_, *this);
        }
        return std::move(this->local_datetime_);
    }
    local_date && as_local_date() &&
    {
        if(this->type_ != value_t::local_date)
        {
            detail::throw_bad_cast<value_t::local_date>(
                    "toml::value::as_local_date(): ", this->type_, *this);
        }
        return std::move(this->local_date_);
    }
    local_time && as_local_time() &&
    {
        if(this->type_ != value_t::local_time)
        {
            detail::throw_bad_cast<value_t::local_time>(
                    "toml::value::as_local_time(): ", this->type_, *this);
        }
        return std::move(this->local_time_);
    }
    array_type && as_array() &&
    {
        if(this->type_ != value_t::array)
        {
            detail::throw_bad_cast<value_t::array>(
                    "toml::value::as_array(): ", this->type_, *this);
        }
        return std::move(this->array_.value());
    }
    table_type && as_table() &&
    {
        if(this->type_ != value_t::table)
        {
            detail::throw_bad_cast<value_t::table>(
                    "toml::value::as_table(): ", this->type_, *this);
        }
        return std::move(this->table_.value());
    }
    // }}}

    // accessors =============================================================
    //
    // may throw type_error or out_of_range
    //
    value_type&       at(const key& k)
    {
        if(!this->is_table())
        {
            detail::throw_bad_cast<value_t::table>(
                "toml::value::at(key): ", this->type_, *this);
        }
        if(this->as_table(std::nothrow).count(k) == 0)
        {
            detail::throw_key_not_found_error(*this, k);
        }
        return this->as_table(std::nothrow).at(k);
    }
    value_type const& at(const key& k) const
    {
        if(!this->is_table())
        {
            detail::throw_bad_cast<value_t::table>(
                "toml::value::at(key): ", this->type_, *this);
        }
        if(this->as_table(std::nothrow).count(k) == 0)
        {
            detail::throw_key_not_found_error(*this, k);
        }
        return this->as_table(std::nothrow).at(k);
    }
    value_type&       operator[](const key& k)
    {
        if(this->is_uninitialized())
        {
            *this = table_type{};
        }
        else if(!this->is_table()) // initialized, but not a table
        {
            detail::throw_bad_cast<value_t::table>(
                "toml::value::operator[](key): ", this->type_, *this);
        }
        return this->as_table(std::nothrow)[k];
    }

    value_type&       at(const std::size_t idx)
    {
        if(!this->is_array())
        {
            detail::throw_bad_cast<value_t::array>(
                "toml::value::at(idx): ", this->type_, *this);
        }
        if(this->as_array(std::nothrow).size() <= idx)
        {
            throw std::out_of_range(detail::format_underline(
                "toml::value::at(idx): no element corresponding to the index", {
                    {this->location(), concat_to_string("the length is ",
                        this->as_array(std::nothrow).size(),
                        ", and the specified index is ", idx)}
                }));
        }
        return this->as_array().at(idx);
    }
    value_type const& at(const std::size_t idx) const
    {
        if(!this->is_array())
        {
            detail::throw_bad_cast<value_t::array>(
                "toml::value::at(idx): ", this->type_, *this);
        }
        if(this->as_array(std::nothrow).size() <= idx)
        {
            throw std::out_of_range(detail::format_underline(
                "toml::value::at(idx): no element corresponding to the index", {
                    {this->location(), concat_to_string("the length is ",
                        this->as_array(std::nothrow).size(),
                        ", and the specified index is ", idx)}
                }));
        }
        return this->as_array(std::nothrow).at(idx);
    }

    value_type&       operator[](const std::size_t idx) noexcept
    {
        // no check...
        return this->as_array(std::nothrow)[idx];
    }
    value_type const& operator[](const std::size_t idx) const noexcept
    {
        // no check...
        return this->as_array(std::nothrow)[idx];
    }

    void push_back(const value_type& x)
    {
        if(!this->is_array())
        {
            detail::throw_bad_cast<value_t::array>(
                "toml::value::push_back(value): ", this->type_, *this);
        }
        this->as_array(std::nothrow).push_back(x);
        return;
    }
    void push_back(value_type&& x)
    {
        if(!this->is_array())
        {
            detail::throw_bad_cast<value_t::array>(
                "toml::value::push_back(value): ", this->type_, *this);
        }
        this->as_array(std::nothrow).push_back(std::move(x));
        return;
    }

    template<typename ... Ts>
    value_type& emplace_back(Ts&& ... args)
    {
        if(!this->is_array())
        {
            detail::throw_bad_cast<value_t::array>(
                "toml::value::emplace_back(...): ", this->type_, *this);
        }
        this->as_array(std::nothrow).emplace_back(std::forward<Ts>(args) ...);
        return this->as_array(std::nothrow).back();
    }

    std::size_t size() const
    {
        switch(this->type_)
        {
            case value_t::array:
            {
                return this->as_array(std::nothrow).size();
            }
            case value_t::table:
            {
                return this->as_table(std::nothrow).size();
            }
            case value_t::string:
            {
                return this->as_string(std::nothrow).str.size();
            }
            default:
            {
                throw type_error(detail::format_underline(
                    "toml::value::size(): bad_cast to container types", {
                        {this->location(),
                         concat_to_string("the actual type is ", this->type_)}
                    }), this->location());
            }
        }
    }

    std::size_t count(const key_type& k) const
    {
        if(!this->is_table())
        {
            detail::throw_bad_cast<value_t::table>(
                "toml::value::count(key): ", this->type_, *this);
        }
        return this->as_table(std::nothrow).count(k);
    }

    bool contains(const key_type& k) const
    {
        if(!this->is_table())
        {
            detail::throw_bad_cast<value_t::table>(
                "toml::value::contains(key): ", this->type_, *this);
        }
        return (this->as_table(std::nothrow).count(k) != 0);
    }

    source_location location() const
    {
        return source_location(this->region_info_.get());
    }

    comment_type const& comments() const noexcept {return this->comments_;}
    comment_type&       comments()       noexcept {return this->comments_;}

  private:

    void cleanup() noexcept
    {
        switch(this->type_)
        {
            case value_t::string : {string_.~string();       return;}
            case value_t::array  : {array_.~array_storage(); return;}
            case value_t::table  : {table_.~table_storage(); return;}
            default              : return;
        }
    }

    // for error messages
    template<typename Value>
    friend region_base const* detail::get_region(const Value& v);

    template<typename Value>
    friend void detail::change_region(Value& v, detail::region reg);

  private:

    using array_storage = detail::storage<array_type>;
    using table_storage = detail::storage<table_type>;

    value_t type_;
    union
    {
        boolean         boolean_;
        integer         integer_;
        floating        floating_;
        string          string_;
        offset_datetime offset_datetime_;
        local_datetime  local_datetime_;
        local_date      local_date_;
        local_time      local_time_;
        array_storage   array_;
        table_storage   table_;
    };
    std::shared_ptr<region_base> region_info_;
    comment_type                 comments_;
};

// default toml::value and default array/table.
// TOML11_DEFAULT_COMMENT_STRATEGY is defined in comments.hpp
using value = basic_value<TOML11_DEFAULT_COMMENT_STRATEGY, std::unordered_map, std::vector>;
using array = typename value::array_type;
using table = typename value::table_type;

template<typename C, template<typename ...> class T, template<typename ...> class A>
inline bool
operator==(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    if(lhs.type()     != rhs.type())     {return false;}
    if(lhs.comments() != rhs.comments()) {return false;}

    switch(lhs.type())
    {
        case value_t::boolean  :
        {
            return lhs.as_boolean() == rhs.as_boolean();
        }
        case value_t::integer  :
        {
            return lhs.as_integer() == rhs.as_integer();
        }
        case value_t::floating :
        {
            return lhs.as_floating() == rhs.as_floating();
        }
        case value_t::string   :
        {
            return lhs.as_string() == rhs.as_string();
        }
        case value_t::offset_datetime:
        {
            return lhs.as_offset_datetime() == rhs.as_offset_datetime();
        }
        case value_t::local_datetime:
        {
            return lhs.as_local_datetime() == rhs.as_local_datetime();
        }
        case value_t::local_date:
        {
            return lhs.as_local_date() == rhs.as_local_date();
        }
        case value_t::local_time:
        {
            return lhs.as_local_time() == rhs.as_local_time();
        }
        case value_t::array    :
        {
            return lhs.as_array() == rhs.as_array();
        }
        case value_t::table    :
        {
            return lhs.as_table() == rhs.as_table();
        }
        case value_t::empty    : {return true; }
        default:                 {return false;}
    }
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
inline bool operator!=(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    return !(lhs == rhs);
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
typename std::enable_if<detail::conjunction<
    detail::is_comparable<typename basic_value<C, T, A>::array_type>,
    detail::is_comparable<typename basic_value<C, T, A>::table_type>
    >::value, bool>::type
operator<(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    if(lhs.type() != rhs.type()){return (lhs.type() < rhs.type());}
    switch(lhs.type())
    {
        case value_t::boolean  :
        {
            return lhs.as_boolean() <  rhs.as_boolean() ||
                  (lhs.as_boolean() == rhs.as_boolean() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::integer  :
        {
            return lhs.as_integer() <  rhs.as_integer() ||
                  (lhs.as_integer() == rhs.as_integer() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::floating :
        {
            return lhs.as_floating() <  rhs.as_floating() ||
                  (lhs.as_floating() == rhs.as_floating() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::string   :
        {
            return lhs.as_string() <  rhs.as_string() ||
                  (lhs.as_string() == rhs.as_string() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::offset_datetime:
        {
            return lhs.as_offset_datetime() <  rhs.as_offset_datetime() ||
                  (lhs.as_offset_datetime() == rhs.as_offset_datetime() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::local_datetime:
        {
            return lhs.as_local_datetime() <  rhs.as_local_datetime() ||
                  (lhs.as_local_datetime() == rhs.as_local_datetime() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::local_date:
        {
            return lhs.as_local_date() <  rhs.as_local_date() ||
                  (lhs.as_local_date() == rhs.as_local_date() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::local_time:
        {
            return lhs.as_local_time() <  rhs.as_local_time() ||
                  (lhs.as_local_time() == rhs.as_local_time() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::array    :
        {
            return lhs.as_array() <  rhs.as_array() ||
                  (lhs.as_array() == rhs.as_array() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::table    :
        {
            return lhs.as_table() <  rhs.as_table() ||
                  (lhs.as_table() == rhs.as_table() &&
                   lhs.comments() < rhs.comments());
        }
        case value_t::empty    :
        {
            return lhs.comments() < rhs.comments();
        }
        default:
        {
            return lhs.comments() < rhs.comments();
        }
    }
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
typename std::enable_if<detail::conjunction<
    detail::is_comparable<typename basic_value<C, T, A>::array_type>,
    detail::is_comparable<typename basic_value<C, T, A>::table_type>
    >::value, bool>::type
operator<=(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    return (lhs < rhs) || (lhs == rhs);
}
template<typename C, template<typename ...> class T, template<typename ...> class A>
typename std::enable_if<detail::conjunction<
    detail::is_comparable<typename basic_value<C, T, A>::array_type>,
    detail::is_comparable<typename basic_value<C, T, A>::table_type>
    >::value, bool>::type
operator>(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    return !(lhs <= rhs);
}
template<typename C, template<typename ...> class T, template<typename ...> class A>
typename std::enable_if<detail::conjunction<
    detail::is_comparable<typename basic_value<C, T, A>::array_type>,
    detail::is_comparable<typename basic_value<C, T, A>::table_type>
    >::value, bool>::type
operator>=(const basic_value<C, T, A>& lhs, const basic_value<C, T, A>& rhs)
{
    return !(lhs < rhs);
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
inline std::string format_error(const std::string& err_msg,
        const basic_value<C, T, A>& v, const std::string& comment,
        std::vector<std::string> hints = {},
        const bool colorize = TOML11_ERROR_MESSAGE_COLORIZED)
{
    return detail::format_underline(err_msg, {{v.location(), comment}},
                                    std::move(hints), colorize);
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
inline std::string format_error(const std::string& err_msg,
        const toml::basic_value<C, T, A>& v1, const std::string& comment1,
        const toml::basic_value<C, T, A>& v2, const std::string& comment2,
        std::vector<std::string> hints = {},
        const bool colorize = TOML11_ERROR_MESSAGE_COLORIZED)
{
    return detail::format_underline(err_msg, {
            {v1.location(), comment1}, {v2.location(), comment2}
        }, std::move(hints), colorize);
}

template<typename C, template<typename ...> class T, template<typename ...> class A>
inline std::string format_error(const std::string& err_msg,
        const toml::basic_value<C, T, A>& v1, const std::string& comment1,
        const toml::basic_value<C, T, A>& v2, const std::string& comment2,
        const toml::basic_value<C, T, A>& v3, const std::string& comment3,
        std::vector<std::string> hints = {},
        const bool colorize = TOML11_ERROR_MESSAGE_COLORIZED)
{
    return detail::format_underline(err_msg, {{v1.location(), comment1},
            {v2.location(), comment2}, {v3.location(), comment3}
        }, std::move(hints), colorize);
}

template<typename Visitor, typename C,
         template<typename ...> class T, template<typename ...> class A>
detail::return_type_of_t<Visitor, const toml::boolean&>
visit(Visitor&& visitor, const toml::basic_value<C, T, A>& v)
{
    switch(v.type())
    {
        case value_t::boolean        : {return visitor(v.as_boolean        ());}
        case value_t::integer        : {return visitor(v.as_integer        ());}
        case value_t::floating       : {return visitor(v.as_floating       ());}
        case value_t::string         : {return visitor(v.as_string         ());}
        case value_t::offset_datetime: {return visitor(v.as_offset_datetime());}
        case value_t::local_datetime : {return visitor(v.as_local_datetime ());}
        case value_t::local_date     : {return visitor(v.as_local_date     ());}
        case value_t::local_time     : {return visitor(v.as_local_time     ());}
        case value_t::array          : {return visitor(v.as_array          ());}
        case value_t::table          : {return visitor(v.as_table          ());}
        case value_t::empty          : break;
        default: break;
    }
    throw std::runtime_error(format_error("[error] toml::visit: toml::basic_value "
            "does not have any valid basic_value.", v, "here"));
}

template<typename Visitor, typename C,
         template<typename ...> class T, template<typename ...> class A>
detail::return_type_of_t<Visitor, toml::boolean&>
visit(Visitor&& visitor, toml::basic_value<C, T, A>& v)
{
    switch(v.type())
    {
        case value_t::boolean        : {return visitor(v.as_boolean        ());}
        case value_t::integer        : {return visitor(v.as_integer        ());}
        case value_t::floating       : {return visitor(v.as_floating       ());}
        case value_t::string         : {return visitor(v.as_string         ());}
        case value_t::offset_datetime: {return visitor(v.as_offset_datetime());}
        case value_t::local_datetime : {return visitor(v.as_local_datetime ());}
        case value_t::local_date     : {return visitor(v.as_local_date     ());}
        case value_t::local_time     : {return visitor(v.as_local_time     ());}
        case value_t::array          : {return visitor(v.as_array          ());}
        case value_t::table          : {return visitor(v.as_table          ());}
        case value_t::empty          : break;
        default: break;
    }
    throw std::runtime_error(format_error("[error] toml::visit: toml::basic_value "
            "does not have any valid basic_value.", v, "here"));
}

template<typename Visitor, typename C,
         template<typename ...> class T, template<typename ...> class A>
detail::return_type_of_t<Visitor, toml::boolean&&>
visit(Visitor&& visitor, toml::basic_value<C, T, A>&& v)
{
    switch(v.type())
    {
        case value_t::boolean        : {return visitor(std::move(v.as_boolean        ()));}
        case value_t::integer        : {return visitor(std::move(v.as_integer        ()));}
        case value_t::floating       : {return visitor(std::move(v.as_floating       ()));}
        case value_t::string         : {return visitor(std::move(v.as_string         ()));}
        case value_t::offset_datetime: {return visitor(std::move(v.as_offset_datetime()));}
        case value_t::local_datetime : {return visitor(std::move(v.as_local_datetime ()));}
        case value_t::local_date     : {return visitor(std::move(v.as_local_date     ()));}
        case value_t::local_time     : {return visitor(std::move(v.as_local_time     ()));}
        case value_t::array          : {return visitor(std::move(v.as_array          ()));}
        case value_t::table          : {return visitor(std::move(v.as_table          ()));}
        case value_t::empty          : break;
        default: break;
    }
    throw std::runtime_error(format_error("[error] toml::visit: toml::basic_value "
            "does not have any valid basic_value.", v, "here"));
}

}// toml
#endif// TOML11_VALUE
