#pragma once

#include<variant>

namespace nix {

/* A These<A,B> is like a std::variant<A,B>, but it also contemplates the
   possibility that we have both values. It takes the name from the analogous
   haskell pattern */

template <typename A>
struct This {
    A left;
    bool operator ==(This<A> thisOther) const noexcept {
        return left == thisOther.left;
    };
    bool operator !=(This<A> thisOther) const noexcept {
        return left != thisOther.left;
    };
};

template <typename A>
struct That {
    A right;
    bool operator ==(That<A> thatOther) const noexcept {
        return right == thatOther.right;
    };
    bool operator !=(That<A> thatOther) const noexcept {
        return right != thatOther.right;
    };
};

template <typename A, typename B>
using These = typename std::variant<This<A>, That<B>, std::pair<A,B>>;

// template <typename A, typename B>
// bool hasFirst(These<A,B> these) {
//     return these.index() == 0 || these.index() == 1;
// }


// template <typename A, typename B>
// std::optional<A> first(These<A,B> these) {
//     if (these.index() == 0)
//         return std::get<A>(these);
//     else if (these.index() == 2) {
//         std::pair<A, B> pair = std::get<std::pair<A,B>>(these);
//         return pair.first;
//     }
//     return std::nullopt;
// }

// template <typename A, typename B>
// std::optional<B> second(These<A,B> these) {
//     if (these.index() == 1)
//         return std::get<B>(these);
//     else if (these.index() == 2) {
//         std::pair<A, B> pair = std::get<std::pair<A,B>>(these);
//         return pair.second;
//     }
//     return std::nullopt;
// }

// // Spec:
// // This a -> These a bNew | This a
// // That b -> That bNew | That b
// // These a b -> These a bNew | These a b
// template <typename A, typename B>
// These<A,B> addOptionalB(These<A,B> these, std::optional<B> maybeB) {
//     if (maybeB) {
//         if (first(these))
//             return (*first(these), *maybeB);
//         else
//             return *maybeB;
//     }
//     return these;
// }

////////////////////////////////////////////////////////////////////////////////
// New implementation
////////////////////////////////////////////////////////////////////////////////

// USE std::visit everywhere
template<typename A, typename B>
struct ViewFirstConst {
    const These<A, B> & ref;
    std::optional<A> operator *() const noexcept {
        return std::visit(overloaded {
            [&](This<A> a) -> std::optional<A> { return a.left; },
            [&](That<B> b) -> std::optional<A> { return std::nullopt; },
            [&](std::pair<A, B> pair) -> std::optional<A> { return pair.first; },
        }, ref);
    };
};

template<typename A, typename B>
struct ViewFirst {

    These<A, B> & ref;

    std::optional<A> operator *() const noexcept {
        return std::visit(overloaded {
            [&](This<A> a) -> std::optional<A> { return a.left; },
            [&](That<B> b) -> std::optional<A> { return std::nullopt; },
            [&](std::pair<A, B> pair) -> std::optional<A> { return pair.first; },
        }, ref);
    };

    void operator =(A newA) noexcept {
         std::visit(overloaded {
             [&](This<A> a) { ref = This<A> { newA }; },
             [&](That<B> b) { ref = std::pair(newA, b.right); },
             [&](std::pair<A, B> pair) { ref = std::pair { newA, pair.second }; },
         }, ref);
        // This a -> This a1
        // That b -> These a1 b
        // These a b -> These a1 b
    };

    void operator =(std::optional<A> optA) {
        if (optA) {
            *this = *optA;
        }
        else {
            std::visit(overloaded {
                [&](This<A> a) { throw "ViewFirst: the new value of A hasn't been supplied"; },
                [&](That<B> b) {},
                [&](std::pair<A, B> pair) { ref = That<B> { pair.second }; },
            }, ref);
        }
        // This a -> This a1 | throw
        // That b -> These a1 b | That b
        // These a b -> These a1 b | That b
    };


    void add(std::optional<A> optA) noexcept {
        if (optA)
            *this = *optA;
        // This a -> This a1 | This a
        // That b -> These a1 b | That b
        // These a b -> These a1 b | These a b
    };

    void modify(std::function<std::optional<A>(std::optional<A>)> f) {
        *this = f(**this);
    }
};

////////////////////////////////////////////////////////////////////////////////
// Second

template<typename A, typename B>
struct ViewSecondConst {
    const These<A, B> & ref;

    std::optional<B> operator *() const noexcept {
        return std::visit(overloaded {
            [&](This<A> a) -> std::optional<B> { return std::nullopt; },
            [&](That<B> b) -> std::optional<B> { return b.right; },
            [&](std::pair<A, B> pair) -> std::optional<B> { return pair.second; },
        }, ref);
    };
};

template<typename A, typename B>
struct ViewSecond {
    These<A, B> & ref;

    std::optional<B> operator *() const noexcept {
        return std::visit(overloaded {
            [&](This<A> a) -> std::optional<B> { return std::nullopt; },
            [&](That<B> b) -> std::optional<B> { return b.right; },
            [&](std::pair<A, B> pair) -> std::optional<B> { return pair.second; },
        }, ref);
    };

    void operator =(B newB) noexcept {
         std::visit(overloaded {
             [&](This<A> a) { ref = std::pair(a.left, newB); },
             [&](That<B> b) { ref = That<B> { newB }; },
             [&](std::pair<A, B> pair) { ref = std::pair { pair.first, newB }; },
         }, ref);
        // This a -> These a bNew
        // That b -> That bNew
        // These a b -> These a bNew
    };

    void operator =(std::optional<B> optB) {
        if (optB) {
            *this = *optB;
        }
        else {
            std::visit(overloaded {
                [&](This<A> a) {},
                [&](That<B> b) { throw "ViewFirst: the new value of B hasn't been supplied"; },
                [&](std::pair<A, B> pair) { ref = This<A> { pair.first }; },
            }, ref);
        }
        // This a -> These a b1 | This a
        // That b -> That b1 | throw
        // These a b -> These a b1 | This a
    };

    void add(std::optional<B> optB) noexcept {
        if (optB)
            *this = *optB;
       // This a -> This a | This a
       // That b -> These a1 b | That b
       // These a b -> These a1 b | These a b
    };
};

////////////////////////////////////////////////////////////////////////////////
// Generating views

template<typename A, typename B>
ViewFirstConst<A, B> viewFirstConst(const These<A, B> & these) {
    return ViewFirstConst<A, B> { .ref = these };
};

template<typename A, typename B>
ViewFirst<A, B> viewFirst(These<A, B> & these) {
    return ViewFirst<A, B> { .ref = these };
};

template<typename A, typename B>
ViewSecondConst<A, B> viewSecondConst(const These<A, B> & these) {
    return ViewSecondConst<A, B> { .ref = these };
}

template<typename A, typename B>
ViewSecond<A, B> viewSecond(These<A, B> & these) {
    return ViewSecond<A, B> { .ref = these };
};

// ViewFirst<HashResult, ContentAddress> ValidPathInfo::nar() {};
// ViewSecond<HashResult, ContentAddress> ValidPathInfo::ca();

}
