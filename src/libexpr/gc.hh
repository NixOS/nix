#pragma once

#include "logging.hh"

#include <stack>
#include <limits>
#include <cassert>

//#define GC_DEBUG 1

namespace nix {

typedef unsigned long Word;

enum Tag {
    tFree = 3,

    // Misc types
    tBindings,
    tValueList,
    tEnv,
    tWithExprEnv,
    tWithAttrsEnv,
    tContext,

    // Value tags
    tInt,
    tBool,
    tShortString,
    tLongString,
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
    constexpr static size_t miscBytes = 7;

public: // FIXME
    Tag type:7;

private:
    bool marked:1;

    unsigned long misc:56;

    void unmark()
    {
        marked = false;
    }

protected:

    Object(Tag type, unsigned long misc) : type(type), marked(false), misc(misc) { }

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

    char * getMiscData() const
    {
        return ((char *) this) + 1;
    }
};

template<class T>
struct PtrList : Object
{
    T * elems[0];

    PtrList(Tag type, size_t size) : Object(type, size)
    {
        for (size_t i = 0; i < size; i++)
            elems[i] = nullptr;
    }

    size_t size() const { return getMisc(); }
    size_t words() const { return wordsFor(size()); }
    static size_t wordsFor(size_t size) { return 1 + size; }
};

struct Free : Object
{
    Free * next;

    Free(size_t size) : Object(tFree, size), next(nullptr) { }

    // return size in words
    size_t words() const { return getMisc(); }

    void setSize(size_t size) { assert(size >= 1); setMisc(size); }
};

template<class T>
struct Ptr;

template<class T>
struct Root;

struct GC
{

private:

    Ptr<Object> * frontPtrSentinel;
    Ptr<Object> * backPtrSentinel;

    Root<Object> * frontRootSentinel;
    Root<Object> * backRootSentinel;

    template<class T>
    friend class Ptr;

    template<class T>
    friend class Root;

    struct Arena
    {
        size_t size; // in words
        Word * start;

        Arena(size_t size)
            : size(size)
            , start(new Word[size])
        {
            assert(size >= 2);
        }

        Arena(const Arena & arena) = delete;

        Arena(Arena && arena)
        {
            size = arena.size;
            start = arena.start;
            arena.start = nullptr;
        }

        ~Arena()
        {
            delete[] start;
        }
    };

    size_t totalSize = 0;
    size_t nextSize;

    std::vector<Arena> arenas;

    struct FreeList
    {
        size_t minSize;
        Free * front = nullptr;
    };

    std::array<FreeList, 8> freeLists;

    size_t allTimeWordsAllocated = 0;
    size_t allTimeWordsFreed = 0;

    Object * allocObject(size_t size)
    {
        assert(size >= 2);

        for (int attempt = 0; attempt < 3; attempt++) {

            for (size_t i = 0; i < freeLists.size(); ++i) {
                auto & freeList = freeLists[i];

                if ((size <= freeList.minSize || i == freeLists.size() - 1) && freeList.front) {
                    //printError("TRY %d %d %d", size, i, freeList.minSize);

                    Free * * prev = &freeList.front;

                    while (Free * freeObj = *prev) {
                        //printError("LOOK %x %d %x", freeObj, freeObj->words(), freeObj->next);
                        assert(freeObj->words() >= freeList.minSize);
                        if (freeObj->words() == size) {
                            // Convert the free object.
                            *prev = freeObj->next;
                            return (Object *) freeObj;
                        } else if (freeObj->words() >= size + 2) {
                            // Split the free object.
                            auto newSize = freeObj->words() - size;
                            freeObj->setSize(newSize);
                            if (newSize < freeList.minSize) {
                                /* The free object is now smaller than
                                   the minimum size for this freelist,
                                   so move it to another one. */
                                //printError("MOVE %x %d -> %d", freeObj, newSize + size, newSize);
                                *prev = freeObj->next;
                                addToFreeList(freeObj);
                            }
                            return (Object *) (((Word *) freeObj) + newSize);
                        } else if (freeObj->words() == size + 1) {
                            // Return the free object and add a padding word.
                            *prev = freeObj->next;
                            freeObj->setSize(1);
                            return (Object *) (((Word *) freeObj) + 1);
                        } else {
                            assert(freeObj->words() < size);
                            prev = &freeObj->next;
                        }
                    }
                }
            }

            if (attempt == 0) {
                debug("allocation of %d bytes failed, GCing...", size * WORD_SIZE);
                gc();
            } else if (attempt == 1) {
                addArena(std::max(nextSize, size));
            }
        }

        throw Error("allocation of %d bytes failed", size);
    }

public:

    GC();
    ~GC();

    template<typename T, typename... Args>
    Ptr<T> alloc(size_t size, const Args & ... args)
    {
        auto raw = allocObject(size);
        allTimeWordsAllocated += size;
        return new (raw) T(args...);
    }

    void gc();

    bool isObject(void * p);

    void assertObject(void * p)
    {
        #if GC_DEBUG
        if (!isObject(p)) {
            printError("object %p is not an object", p);
            abort();
        }
        #endif
    }

private:

    void addArena(size_t arenaSize);

    void addToFreeList(Free * obj);

    std::pair<size_t, size_t> freeUnmarked(Arena & arena);
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
        prev = (Ptr *) gc.frontPtrSentinel;
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
