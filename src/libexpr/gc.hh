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

template<class T>
struct Ptr
{
    Ptr * prev = nullptr, * next = nullptr;
    T * value = nullptr;

    Ptr() { }

    Ptr(Ptr * next, T * value) : next(next), value(value)
    {
        assert(value);
        assert(next == next->prev->next);
        prev = next->prev;
        next->prev = this;
        prev->next = this;
    }

    Ptr(const Ptr & p)
    {
        if (p.value) {
            auto & p2 = const_cast<Ptr &>(p);
            value = p2.value;
            next = &p2;
            prev = p2.prev;
            prev->next = this;
            p2.prev = this;
        }
    }

    Ptr(Ptr && p)
    {
        *this = std::move(p);
    }

    Ptr & operator =(Ptr && p)
    {
        reset();
        if (p.value) {
            value = p.value;
            next = p.next;
            prev = p.prev;
            p.value = nullptr;
            prev->next = this;
            next->prev = this;
        }
        return *this;
    }

    Ptr & operator =(const Ptr & p)
    {
        throw Error("NOT IMPLEMENTED = PTR &");
    }

    ~Ptr()
    {
        reset();
    }

    void reset()
    {
        if (value) {
            assert(next);
            assert(prev);
            assert(next->prev == this);
            next->prev = prev;
            assert(prev->next == this);
            prev->next = next;
            value = nullptr;
        }
    }

    T * operator ->()
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
};

struct Free : Object
{
    Free * next;

    Free(Size size) : Object(tFree, size), next(nullptr) { }

    // return size in words
    Size words() const { return getMisc(); }

    void setSize(Size size) { assert(size >= 1); setMisc(size); }
};

struct GC
{

private:

    Ptr<Object> * frontSentinel;
    Ptr<Object> * backSentinel;

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

    std::array<ArenaList, 2> arenaLists;

public:

    GC();
    ~GC();

    template<typename T, typename... Args>
    Ptr<T> alloc(Size size, const Args & ... args)
    {
        ArenaList & arenaList = size == 3 ? arenaLists[0] : arenaLists[1];

        for (int i = 0; i < 3; i++) {

            for (auto j = arenaList.arenas.rbegin(); j != arenaList.arenas.rend(); ++j) {
                auto & arena = *j;
                auto raw = arena.alloc(size);
                if (raw) {
                    auto obj = new (raw) T(args...);
                    //printError("ALLOC %x", obj);
                    return Ptr<T>((Ptr<T> *) frontSentinel->next, obj);
                }
            }

            if (i == 0) {
                printError("allocation of %d bytes failed, GCing...", size * WORD_SIZE);
                gc();
            } else {
                Size arenaSize = std::max(arenaList.nextSize, size);
                arenaList.nextSize = arenaSize * 2; // FIXME: overflow
                printError("allocating arena of %d bytes", arenaSize * WORD_SIZE);
                arenaList.arenas.emplace_back(arenaSize);
            }
        }

        throw Error("allocation of %d bytes failed", size);
    }

    void gc();

    bool isObject(void * p);
};

extern GC gc;

}
