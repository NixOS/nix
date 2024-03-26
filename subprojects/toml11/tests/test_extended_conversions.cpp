#include <toml.hpp>

#include "unit_test.hpp"

#include <deque>
#include <map>

namespace extlib
{
struct foo
{
    int a;
    std::string b;
};
struct bar
{
    int a;
    std::string b;

    void from_toml(const toml::value& v)
    {
        this->a = toml::find<int>(v, "a");
        this->b = toml::find<std::string>(v, "b");
        return ;
    }

    toml::table into_toml() const
    {
        return toml::table{{"a", this->a}, {"b", this->b}};
    }
};

struct baz
{
    int a;
    std::string b;
};
struct qux
{
    int a;
    std::string b;
};

struct foobar
{
    // via constructor
    explicit foobar(const toml::value& v)
        : a(toml::find<int>(v, "a")), b(toml::find<std::string>(v, "b"))
    {}
    int a;
    std::string b;
};
} // extlib

namespace toml
{
template<>
struct from<extlib::foo>
{
    static extlib::foo from_toml(const toml::value& v)
    {
        return extlib::foo{toml::find<int>(v, "a"), toml::find<std::string>(v, "b")};
    }
};

template<>
struct into<extlib::foo>
{
    static toml::value into_toml(const extlib::foo& f)
    {
        return toml::value{{"a", f.a}, {"b", f.b}};
    }
};

template<>
struct from<extlib::baz>
{
    static extlib::baz from_toml(const toml::value& v)
    {
        return extlib::baz{toml::find<int>(v, "a"), toml::find<std::string>(v, "b")};
    }
};

template<>
struct into<extlib::qux>
{
    static toml::table into_toml(const extlib::qux& f)
    {
        return toml::table{{"a", f.a}, {"b", f.b}};
    }
};
} // toml

// ---------------------------------------------------------------------------

namespace extlib2
{
struct foo
{
    int a;
    std::string b;
};
struct bar
{
    int a;
    std::string b;

    template<typename C, template<typename ...> class M, template<typename ...> class A>
    void from_toml(const toml::basic_value<C, M, A>& v)
    {
        this->a = toml::find<int>(v, "a");
        this->b = toml::find<std::string>(v, "b");
        return ;
    }

    toml::table into_toml() const
    {
        return toml::table{{"a", this->a}, {"b", this->b}};
    }
};
struct baz
{
    int a;
    std::string b;
};
struct qux
{
    int a;
    std::string b;
};

struct foobar
{
    template<typename C, template<typename ...> class M, template<typename ...> class A>
    explicit foobar(const toml::basic_value<C, M, A>& v)
        : a(toml::find<int>(v, "a")), b(toml::find<std::string>(v, "b"))
    {}
    int a;
    std::string b;
};

} // extlib2

namespace toml
{
template<>
struct from<extlib2::foo>
{
    template<typename C, template<typename ...> class M, template<typename ...> class A>
    static extlib2::foo from_toml(const toml::basic_value<C, M, A>& v)
    {
        return extlib2::foo{toml::find<int>(v, "a"), toml::find<std::string>(v, "b")};
    }
};

template<>
struct into<extlib2::foo>
{
    static toml::table into_toml(const extlib2::foo& f)
    {
        return toml::table{{"a", f.a}, {"b", f.b}};
    }
};

template<>
struct from<extlib2::baz>
{
    template<typename C, template<typename ...> class M, template<typename ...> class A>
    static extlib2::baz from_toml(const toml::basic_value<C, M, A>& v)
    {
        return extlib2::baz{toml::find<int>(v, "a"), toml::find<std::string>(v, "b")};
    }
};

template<>
struct into<extlib2::qux>
{
    static toml::basic_value<toml::preserve_comments, std::map>
    into_toml(const extlib2::qux& f)
    {
        return toml::basic_value<toml::preserve_comments, std::map>{
            {"a", f.a}, {"b", f.b}
        };
    }
};
} // toml

// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(test_conversion_by_member_methods)
{
    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto foo = toml::get<extlib::foo>(v);
        BOOST_TEST(foo.a == 42);
        BOOST_TEST(foo.b == "baz");

        const toml::value v2(foo);

        BOOST_TEST(v == v2);
    }

    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto foo = toml::get<extlib2::foo>(v);
        BOOST_TEST(foo.a == 42);
        BOOST_TEST(foo.b == "baz");

        const toml::value v2(foo);
        BOOST_TEST(v == v2);
    }

    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v{{"a", 42}, {"b", "baz"}};

        const auto foo = toml::get<extlib2::foo>(v);
        BOOST_TEST(foo.a == 42);
        BOOST_TEST(foo.b == "baz");

        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v2(foo);

        BOOST_TEST(v == v2);
    }
}

BOOST_AUTO_TEST_CASE(test_conversion_by_specialization)
{
    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto bar = toml::get<extlib::bar>(v);
        BOOST_TEST(bar.a == 42);
        BOOST_TEST(bar.b == "baz");

        const toml::value v2(bar);

        BOOST_TEST(v == v2);
    }
    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto bar = toml::get<extlib2::bar>(v);
        BOOST_TEST(bar.a == 42);
        BOOST_TEST(bar.b == "baz");

        const toml::value v2(bar);

        BOOST_TEST(v == v2);
    }
    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v{{"a", 42}, {"b", "baz"}};

        const auto bar = toml::get<extlib2::bar>(v);
        BOOST_TEST(bar.a == 42);
        BOOST_TEST(bar.b == "baz");

        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v2(bar);

        BOOST_TEST(v == v2);
    }
}

BOOST_AUTO_TEST_CASE(test_conversion_one_way)
{
    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto baz = toml::get<extlib::baz>(v);
        BOOST_TEST(baz.a == 42);
        BOOST_TEST(baz.b == "baz");
    }
    {
        const extlib::qux q{42, "qux"};
        const toml::value v(q);

        BOOST_TEST(toml::find<int>(v, "a")         == 42);
        BOOST_TEST(toml::find<std::string>(v, "b") == "qux");
    }

    {
        const toml::basic_value<toml::discard_comments, std::map> v{
            {"a", 42}, {"b", "baz"}
        };

        const auto baz = toml::get<extlib2::baz>(v);
        BOOST_TEST(baz.a == 42);
        BOOST_TEST(baz.b == "baz");
    }
    {
        const extlib::qux q{42, "qux"};
        const toml::basic_value<toml::preserve_comments, std::map> v(q);

        BOOST_TEST(toml::find<int>(v, "a")         == 42);
        BOOST_TEST(toml::find<std::string>(v, "b") == "qux");
    }
}

BOOST_AUTO_TEST_CASE(test_conversion_via_constructor)
{
    {
        const toml::value v{{"a", 42}, {"b", "foobar"}};

        const auto foobar = toml::get<extlib::foobar>(v);
        BOOST_TEST(foobar.a == 42);
        BOOST_TEST(foobar.b == "foobar");
    }

    {
        const toml::basic_value<toml::discard_comments, std::map> v{
            {"a", 42}, {"b", "foobar"}
        };

        const auto foobar = toml::get<extlib2::foobar>(v);
        BOOST_TEST(foobar.a == 42);
        BOOST_TEST(foobar.b == "foobar");
    }
}

BOOST_AUTO_TEST_CASE(test_recursive_conversion)
{
    {
        const toml::value v{
            toml::table{{"a", 42}, {"b", "baz"}},
            toml::table{{"a", 43}, {"b", "qux"}},
            toml::table{{"a", 44}, {"b", "quux"}},
            toml::table{{"a", 45}, {"b", "foobar"}},
        };

        const auto foos = toml::get<std::vector<extlib::foo>>(v);
        BOOST_TEST(foos.size()  == 4ul);
        BOOST_TEST(foos.at(0).a == 42);
        BOOST_TEST(foos.at(1).a == 43);
        BOOST_TEST(foos.at(2).a == 44);
        BOOST_TEST(foos.at(3).a == 45);

        BOOST_TEST(foos.at(0).b == "baz");
        BOOST_TEST(foos.at(1).b == "qux");
        BOOST_TEST(foos.at(2).b == "quux");
        BOOST_TEST(foos.at(3).b == "foobar");

        const auto bars = toml::get<std::vector<extlib::bar>>(v);
        BOOST_TEST(bars.size()  == 4ul);
        BOOST_TEST(bars.at(0).a == 42);
        BOOST_TEST(bars.at(1).a == 43);
        BOOST_TEST(bars.at(2).a == 44);
        BOOST_TEST(bars.at(3).a == 45);

        BOOST_TEST(bars.at(0).b == "baz");
        BOOST_TEST(bars.at(1).b == "qux");
        BOOST_TEST(bars.at(2).b == "quux");
        BOOST_TEST(bars.at(3).b == "foobar");
    }
    {
        const toml::value v{
                toml::table{{"a", 42}, {"b", "baz"}},
                toml::table{{"a", 43}, {"b", "qux"}},
                toml::table{{"a", 44}, {"b", "quux"}},
                toml::table{{"a", 45}, {"b", "foobar"}},
            };

        const auto foos = toml::get<std::vector<extlib2::foo>>(v);
        BOOST_TEST(foos.size()  == 4ul);
        BOOST_TEST(foos.at(0).a == 42);
        BOOST_TEST(foos.at(1).a == 43);
        BOOST_TEST(foos.at(2).a == 44);
        BOOST_TEST(foos.at(3).a == 45);

        BOOST_TEST(foos.at(0).b == "baz");
        BOOST_TEST(foos.at(1).b == "qux");
        BOOST_TEST(foos.at(2).b == "quux");
        BOOST_TEST(foos.at(3).b == "foobar");

        const auto bars = toml::get<std::vector<extlib2::bar>>(v);
        BOOST_TEST(bars.size()  == 4ul);
        BOOST_TEST(bars.at(0).a == 42);
        BOOST_TEST(bars.at(1).a == 43);
        BOOST_TEST(bars.at(2).a == 44);
        BOOST_TEST(bars.at(3).a == 45);

        BOOST_TEST(bars.at(0).b == "baz");
        BOOST_TEST(bars.at(1).b == "qux");
        BOOST_TEST(bars.at(2).b == "quux");
        BOOST_TEST(bars.at(3).b == "foobar");
    }

    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v{
                toml::table{{"a", 42}, {"b", "baz"}},
                toml::table{{"a", 43}, {"b", "qux"}},
                toml::table{{"a", 44}, {"b", "quux"}},
                toml::table{{"a", 45}, {"b", "foobar"}}
            };

        const auto foos = toml::get<std::vector<extlib2::foo>>(v);
        BOOST_TEST(foos.size()  == 4ul);
        BOOST_TEST(foos.at(0).a == 42);
        BOOST_TEST(foos.at(1).a == 43);
        BOOST_TEST(foos.at(2).a == 44);
        BOOST_TEST(foos.at(3).a == 45);

        BOOST_TEST(foos.at(0).b == "baz");
        BOOST_TEST(foos.at(1).b == "qux");
        BOOST_TEST(foos.at(2).b == "quux");
        BOOST_TEST(foos.at(3).b == "foobar");

        const auto bars = toml::get<std::vector<extlib2::bar>>(v);
        BOOST_TEST(bars.size()  == 4ul);
        BOOST_TEST(bars.at(0).a == 42);
        BOOST_TEST(bars.at(1).a == 43);
        BOOST_TEST(bars.at(2).a == 44);
        BOOST_TEST(bars.at(3).a == 45);

        BOOST_TEST(bars.at(0).b == "baz");
        BOOST_TEST(bars.at(1).b == "qux");
        BOOST_TEST(bars.at(2).b == "quux");
        BOOST_TEST(bars.at(3).b == "foobar");
    }

    // via constructor
    {
        const toml::value v{
                toml::table{{"a", 42}, {"b", "baz"}},
                toml::table{{"a", 43}, {"b", "qux"}},
                toml::table{{"a", 44}, {"b", "quux"}},
                toml::table{{"a", 45}, {"b", "foobar"}}
            };

        {
            const auto foobars = toml::get<std::vector<extlib::foobar>>(v);
            BOOST_TEST(foobars.size()  == 4ul);
            BOOST_TEST(foobars.at(0).a == 42);
            BOOST_TEST(foobars.at(1).a == 43);
            BOOST_TEST(foobars.at(2).a == 44);
            BOOST_TEST(foobars.at(3).a == 45);

            BOOST_TEST(foobars.at(0).b == "baz");
            BOOST_TEST(foobars.at(1).b == "qux");
            BOOST_TEST(foobars.at(2).b == "quux");
            BOOST_TEST(foobars.at(3).b == "foobar");
        }
        {
            const auto foobars = toml::get<std::vector<extlib2::foobar>>(v);
            BOOST_TEST(foobars.size()  == 4ul);
            BOOST_TEST(foobars.at(0).a == 42);
            BOOST_TEST(foobars.at(1).a == 43);
            BOOST_TEST(foobars.at(2).a == 44);
            BOOST_TEST(foobars.at(3).a == 45);

            BOOST_TEST(foobars.at(0).b == "baz");
            BOOST_TEST(foobars.at(1).b == "qux");
            BOOST_TEST(foobars.at(2).b == "quux");
            BOOST_TEST(foobars.at(3).b == "foobar");
        }
    }
    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v{
                toml::table{{"a", 42}, {"b", "baz"}},
                toml::table{{"a", 43}, {"b", "qux"}},
                toml::table{{"a", 44}, {"b", "quux"}},
                toml::table{{"a", 45}, {"b", "foobar"}}
            };

        const auto foobars = toml::get<std::vector<extlib2::foobar>>(v);
        BOOST_TEST(foobars.size()  == 4ul);
        BOOST_TEST(foobars.at(0).a == 42);
        BOOST_TEST(foobars.at(1).a == 43);
        BOOST_TEST(foobars.at(2).a == 44);
        BOOST_TEST(foobars.at(3).a == 45);

        BOOST_TEST(foobars.at(0).b == "baz");
        BOOST_TEST(foobars.at(1).b == "qux");
        BOOST_TEST(foobars.at(2).b == "quux");
        BOOST_TEST(foobars.at(3).b == "foobar");
    }

    // via constructor
    {
        const toml::value v{
                {"0", toml::table{{"a", 42}, {"b", "baz"}}},
                {"1", toml::table{{"a", 43}, {"b", "qux"}}},
                {"2", toml::table{{"a", 44}, {"b", "quux"}}},
                {"3", toml::table{{"a", 45}, {"b", "foobar"}}}
            };

        {
            const auto foobars = toml::get<std::map<std::string, extlib::foobar>>(v);
            BOOST_TEST(foobars.size()  == 4ul);
            BOOST_TEST(foobars.at("0").a == 42);
            BOOST_TEST(foobars.at("1").a == 43);
            BOOST_TEST(foobars.at("2").a == 44);
            BOOST_TEST(foobars.at("3").a == 45);

            BOOST_TEST(foobars.at("0").b == "baz");
            BOOST_TEST(foobars.at("1").b == "qux");
            BOOST_TEST(foobars.at("2").b == "quux");
            BOOST_TEST(foobars.at("3").b == "foobar");
        }
        {
            const auto foobars = toml::get<std::map<std::string, extlib2::foobar>>(v);
            BOOST_TEST(foobars.size()  == 4ul);
            BOOST_TEST(foobars.at("0").a == 42);
            BOOST_TEST(foobars.at("1").a == 43);
            BOOST_TEST(foobars.at("2").a == 44);
            BOOST_TEST(foobars.at("3").a == 45);

            BOOST_TEST(foobars.at("0").b == "baz");
            BOOST_TEST(foobars.at("1").b == "qux");
            BOOST_TEST(foobars.at("2").b == "quux");
            BOOST_TEST(foobars.at("3").b == "foobar");
        }
    }
    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque>
            v{
                {"0", toml::table{{"a", 42}, {"b", "baz"}}},
                {"1", toml::table{{"a", 43}, {"b", "qux"}}},
                {"2", toml::table{{"a", 44}, {"b", "quux"}}},
                {"3", toml::table{{"a", 45}, {"b", "foobar"}}}
            };

        const auto foobars = toml::get<std::map<std::string, extlib::foobar>>(v);
        BOOST_TEST(foobars.size()  == 4ul);
        BOOST_TEST(foobars.at("0").a == 42);
        BOOST_TEST(foobars.at("1").a == 43);
        BOOST_TEST(foobars.at("2").a == 44);
        BOOST_TEST(foobars.at("3").a == 45);

        BOOST_TEST(foobars.at("0").b == "baz");
        BOOST_TEST(foobars.at("1").b == "qux");
        BOOST_TEST(foobars.at("2").b == "quux");
        BOOST_TEST(foobars.at("3").b == "foobar");
    }

}

// ===========================================================================

#ifndef TOML11_WITHOUT_DEFINE_NON_INTRUSIVE

namespace extlib3
{
struct foo
{
    int a;
    std::string b;
};
struct bar
{
    int         a;
    std::string b;
    foo         f;
};

} // extlib3

TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(extlib3::foo, a, b)
TOML11_DEFINE_CONVERSION_NON_INTRUSIVE(extlib3::bar, a, b, f)

BOOST_AUTO_TEST_CASE(test_conversion_via_macro)
{
    {
        const toml::value v{{"a", 42}, {"b", "baz"}};

        const auto foo = toml::get<extlib3::foo>(v);
        BOOST_TEST(foo.a == 42);
        BOOST_TEST(foo.b == "baz");

        const toml::value v2(foo);
        BOOST_TEST(v2 == v);
    }
    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque> v{
            {"a", 42}, {"b", "baz"}
        };

        const auto foo = toml::get<extlib3::foo>(v);
        BOOST_TEST(foo.a == 42);
        BOOST_TEST(foo.b == "baz");

        const toml::basic_value<toml::discard_comments, std::map, std::deque> v2(foo);
        BOOST_TEST(v2 == v);
    }

    // -----------------------------------------------------------------------

    {
        const toml::value v{
            {"a", 42},
            {"b", "bar.b"},
            {"f", toml::table{{"a", 42}, {"b", "foo.b"}}}
        };

        const auto bar = toml::get<extlib3::bar>(v);
        BOOST_TEST(bar.a == 42);
        BOOST_TEST(bar.b == "bar.b");
        BOOST_TEST(bar.f.a == 42);
        BOOST_TEST(bar.f.b == "foo.b");

        const toml::value v2(bar);
        BOOST_TEST(v2 == v);
    }
    {
        const toml::basic_value<toml::discard_comments, std::map, std::deque> v{
            {"a", 42},
            {"b", "bar.b"},
            {"f", toml::table{{"a", 42}, {"b", "foo.b"}}}
        };

        const auto bar = toml::get<extlib3::bar>(v);
        BOOST_TEST(bar.a == 42);
        BOOST_TEST(bar.b == "bar.b");
        BOOST_TEST(bar.f.a == 42);
        BOOST_TEST(bar.f.b == "foo.b");

        const toml::basic_value<toml::discard_comments, std::map, std::deque> v2(bar);
        BOOST_TEST(v2 == v);
    }
}
#endif // TOML11_WITHOUT_DEFINE_NON_INTRUSIVE
