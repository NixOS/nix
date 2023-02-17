#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/expr/value.hh"
#include "nix/expr/nixexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"

#include "tests/libstore.hh"

namespace nix {
    class LibExprTest : public LibStoreTest {
        public:
            static void SetUpTestSuite() {
                LibStoreTest::SetUpTestSuite();
                initGC();
            }

        protected:
            LibExprTest()
                : LibStoreTest()
                , state({}, store)
            {
            }
            Value eval(std::string input, bool forceValue = true) {
                Value v;
                Expr * e = state.parseExprFromString(input, "");
                assert(e);
                state.eval(e, v);
                if (forceValue)
                    state.forceValue(v, noPos);
                return v;
            }

            Symbol createSymbol(const char * value) {
                return state.symbols.create(value);
            }

            EvalState state;
    };

    MATCHER(IsListType, "") {
        return arg != nList;
    }

    MATCHER(IsList, "") {
        return arg.type() == nList;
    }

    MATCHER(IsString, "") {
        return arg.type() == nString;
    }

    MATCHER(IsNull, "") {
        return arg.type() == nNull;
    }

    MATCHER(IsThunk, "") {
        return arg.type() == nThunk;
    }

    MATCHER(IsAttrs, "") {
        return arg.type() == nAttrs;
    }

    MATCHER_P(IsStringEq, s, fmt("The string is equal to \"%1%\"", s)) {
        if (arg.type() != nString) {
            return false;
        }
        return std::string_view(arg.string.s) == s;
    }

    MATCHER_P(IsIntEq, v, fmt("The string is equal to \"%1%\"", v)) {
        if (arg.type() != nInt) {
            return false;
        }
        return arg.integer == v;
    }

    MATCHER_P(IsFloatEq, v, fmt("The float is equal to \"%1%\"", v)) {
        if (arg.type() != nFloat) {
            return false;
        }
        return arg.fpoint == v;
    }

    MATCHER(IsTrue, "") {
        if (arg.type() != nBool) {
            return false;
        }
        return arg.boolean == true;
    }

    MATCHER(IsFalse, "") {
        if (arg.type() != nBool) {
            return false;
        }
        return arg.boolean == false;
    }

    MATCHER_P(IsPathEq, p, fmt("Is a path equal to \"%1%\"", p)) {
            if (arg.type() != nPath) {
                *result_listener << "Expected a path got " << arg.type();
                return false;
            } else if (std::string_view(arg.string.s) != p) {
                *result_listener << "Expected a path that equals \"" << p << "\" but got: " << arg.string.s;
                return false;
            }
            return true;
    }


    MATCHER_P(IsListOfSize, n, fmt("Is a list of size [%1%]", n)) {
        if (arg.type() != nList) {
            *result_listener << "Expected list got " << arg.type();
            return false;
        } else if (arg.listSize() != (size_t)n) {
            *result_listener << "Expected as list of size " << n << " got " << arg.listSize();
            return false;
        }
        return true;
    }

    MATCHER_P(IsAttrsOfSize, n, fmt("Is a set of size [%1%]", n)) {
        if (arg.type() != nAttrs) {
            *result_listener << "Expected set got " << arg.type();
            return false;
        } else if (arg.attrs->size() != (size_t)n) {
            *result_listener << "Expected a set with " << n << " attributes but got " << arg.attrs->size();
            return false;
        }
        return true;
    }


} /* namespace nix */
