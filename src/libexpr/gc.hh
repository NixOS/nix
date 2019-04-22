#pragma once

#include "logging.hh"

#include <stack>
#include <limits>
#include <cassert>

namespace nix {

typedef unsigned long Word;
typedef size_t Size; // size in words

enum Tag {
    tFree = 3,

    // Misc types
    tBindings,
    tValueList,
    tEnv,
    tWithExprEnv,
    tWithAttrsEnv,

    // Value tags
    tInt,
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList0,
    tList1,
    tList2,
    tListN,
    tThunk,
    tApp,
    tLambda,
    tBlackhole,
    tPrimOp,
    tPrimOpApp,
    tExternal,
    tFloat
};

constexpr size_t WORD_SIZE = 8;

struct Object
{
    friend class GC;

public:
    constexpr static unsigned int miscBits = 58;

private:
    unsigned long misc:58;

public: // FIXME
    Tag type:5;

private:
    bool marked:1;

    void unmark()
    {
        marked = false;
    }

protected:

    Object(Tag type, unsigned long misc) : misc(misc), type(type), marked(false) { }

    bool isMarked()
    {
        return marked;
    }

    void mark()
    {
        marked = true;
    }

    void setMisc(unsigned int misc)
    {
        this->misc = misc;
    }

    unsigned int getMisc() const
    {
        return misc;
    }
};

template<class T>
struct PtrList : Object
{
    T * elems[0];

    PtrList(Tag type, Size size) : Object(type, size)
    {
        for (Size i = 0; i < size; i++)
            elems[i] = nullptr;
    }

    Size size() const { return getMisc(); }
    Size words() const { return wordsFor(size()); }
    static Size wordsFor(Size size) { return 1 + size; }
};

struct Free : Object
{
    Free * next;

    Free(Size size) : Object(tFree, size), next(nullptr) { }

    // return size in words
    Size words() const { return getMisc(); }

    void setSize(Size size) { assert(size >= 1); setMisc(size); }
};

template<class T>
struct Ptr;

template<class T>
struct Root;

struct GC
{

private:

    Ptr<Object> * frontSentinel;
    Ptr<Object> * backSentinel;

    Root<Object> * frontRootSentinel;
    Root<Object> * backRootSentinel;

    template<class T>
    friend class Ptr;

    template<class T>
    friend class Root;

    struct Arena
    {
        Size size; // in words
        Size free; // words free
        Free * firstFree;
        Word * start;

        Arena(Size size)
            : size(size)
            , free(size)
            , start(new Word[size])
        {
            assert(size >= 2);
            firstFree = new (start) Free(size);
        }

        Arena(const Arena & arena) = delete;

        Arena(Arena && arena)
        {
            *this = std::move(arena);
        }

        Arena & operator =(Arena && arena)
        {
            size = arena.size;
            free = arena.free;
            firstFree = arena.firstFree;
            start = arena.start;
            arena.start = nullptr;
            return *this;
        }

        ~Arena()
        {
            delete[] start;
        }

        Object * alloc(Size size)
        {
            assert(size >= 2);

            Free * * prev = &firstFree;

            while (Free * freeObj = *prev) {
                //printError("LOOK %x %d %x", freeObj, freeObj->words(), freeObj->next);
                assert(freeObj->words() >= 2);
                if (freeObj->words() == size) {
                    *prev = freeObj->next;
                    assert(free >= size);
                    free -= size;
                    return (Object *) freeObj;
                } else if (freeObj->words() >= size + 2) {
                    // Split this free object.
                    auto newSize = freeObj->words() - size;
                    freeObj->setSize(newSize);
                    assert(free >= size);
                    free -= size;
                    return (Object *) (((Word *) freeObj) + newSize);
                } else if (freeObj->words() == size + 1) {
                    // Return this free object and add a padding word.
                    *prev = freeObj->next;
                    freeObj->setSize(1);
                    assert(free >= size + 1);
                    free -= size + 1;
                    return (Object *) (((Word *) freeObj) + 1);
                } else {
                    assert(freeObj->words() < size);
                    prev = &freeObj->next;
                }
            }

            return nullptr;
        }

        std::pair<Size, Size> freeUnmarked();
    };

    // Note: arenas are sorted by ascending amount of free space.
    struct ArenaList
    {
        Size nextSize;
        std::vector<Arena> arenas;
        ArenaList();
    };

    std::array<ArenaList, 3> arenaLists;

public:

    GC();
    ~GC();

    template<typename T, typename... Args>
    Ptr<T> alloc(Size size, const Args & ... args)
    {
        ArenaList & arenaList =
            size == 3 ? arenaLists[0] :
            size == 4 ? arenaLists[1] :
            arenaLists[2];

        for (int i = 0; i < 3; i++) {

            for (auto j = arenaList.arenas.rbegin(); j != arenaList.arenas.rend(); ++j) {
                auto & arena = *j;
                auto raw = arena.alloc(size);
                if (raw) {
                    auto obj = new (raw) T(args...);
                    return obj;
                }
            }

            if (i == 0) {
                debug("allocation of %d bytes failed, GCing...", size * WORD_SIZE);
                gc();
            } else {
                Size arenaSize = std::max(arenaList.nextSize, size);
                arenaList.nextSize = arenaSize * 1.5; // FIXME: overflow
                debug("allocating arena of %d bytes", arenaSize * WORD_SIZE);
                arenaList.arenas.emplace_back(arenaSize);
            }
        }

        throw Error("allocation of %d bytes failed", size);
    }

    void gc();

    bool isObject(void * p);

    void assertObject(void * p);
};

extern GC gc;

template<class T>
struct Ptr
{
private:

    friend class GC;

    Ptr * prev = nullptr, * next = nullptr;
    T * value = nullptr;

    void link()
    {
        prev = (Ptr *) gc.frontSentinel;
        next = prev->next;
        next->prev = this;
        prev->next = this;
    }

public:

    Ptr() {
        link();
    }

    Ptr(T * value) : value(value)
    {
        link();
    }

    Ptr(const Ptr & p)
    {
        auto & p2 = const_cast<Ptr &>(p);
        value = p2.value;
        next = &p2;
        prev = p2.prev;
        prev->next = this;
        p2.prev = this;
    }

    Ptr(Ptr && p)
    {
        link();
        value = p.value;
        p.value = nullptr;
    }

    Ptr & operator =(const Ptr & p)
    {
        value = p.value;
        return *this;
    }

    Ptr & operator =(Ptr && p)
    {
        value = p.value;
        p.value = nullptr;
        return *this;
    }

    Ptr & operator =(T * v)
    {
        value = v;
        return *this;
    }

    ~Ptr()
    {
        assert(next);
        assert(prev);
        assert(next->prev == this);
        next->prev = prev;
        assert(prev->next == this);
        prev->next = next;
    }

    T * operator ->()
    {
        return value;
    }

    T * operator ->() const // FIXME
    {
        return value;
    }

    operator T * ()
    {
        return value;
    }

    operator T & ()
    {
        assert(value);
        return *value;
    }

    operator bool() const
    {
        return value != nullptr;
    }
};

template<class T>
struct Root
{
    Root * prev = nullptr, * next = nullptr;
    T value;

    template<typename... Args>
    Root(const Args & ... args)
        : value{args... }
    {
        prev = (Root *) gc.frontRootSentinel;
        next = prev->next;
        next->prev = this;
        prev->next = this;
    }

    Root(const Root & p) = delete;
    Root(Root && p) = delete;

    Root & operator =(const T & v) { value = v; return *this; }

    ~Root()
    {
        assert(next);
        assert(prev);
        assert(next->prev == this);
        next->prev = prev;
        assert(prev->next == this);
        prev->next = next;
    }

    T * operator ->()
    {
        return &value;
    }

    operator T * ()
    {
        return &value;
    }

    operator T & ()
    {
        return value;
    }
};

}
