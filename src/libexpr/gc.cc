#include "gc.hh"
#include "value.hh"
#include "attr-set.hh"
#include "eval.hh"

#include <chrono>

namespace nix {

GC gc;

GC::GC()
{
    nextSize = std::max((size_t) 2, parseSize<size_t>(getEnv("GC_INITIAL_HEAP_SIZE").value_or("131072")) / WORD_SIZE);

    // FIXME: placement new
    frontPtrSentinel = (Ptr<Object> *) malloc(sizeof(Ptr<Object>));
    backPtrSentinel = (Ptr<Object> *) malloc(sizeof(Ptr<Object>));

    frontPtrSentinel->prev = nullptr;
    frontPtrSentinel->next = backPtrSentinel;

    backPtrSentinel->prev = frontPtrSentinel;
    backPtrSentinel->next = nullptr;

    frontRootSentinel = (Root<Object> *) malloc(sizeof(Root<Object>));
    backRootSentinel = (Root<Object> *) malloc(sizeof(Root<Object>));

    frontRootSentinel->prev = nullptr;
    frontRootSentinel->next = backRootSentinel;

    backRootSentinel->prev = frontRootSentinel;
    backRootSentinel->next = nullptr;

    freeLists[0].minSize = 2;
    freeLists[1].minSize = 3;
    freeLists[2].minSize = 4;
    freeLists[3].minSize = 8;
    freeLists[4].minSize = 16;
    freeLists[5].minSize = 32;
    freeLists[6].minSize = 64;
    freeLists[7].minSize = 128;

    addArena(nextSize);
}

GC::~GC()
{
    debug("%d bytes in arenas, %d bytes allocated, %d bytes reclaimed, in %d ms",
        totalSize * WORD_SIZE,
        allTimeWordsAllocated * WORD_SIZE,
        allTimeWordsFreed * WORD_SIZE,
        totalDurationMs);

    size_t n = 0;
    for (Ptr<Object> * p = frontPtrSentinel->next; p != backPtrSentinel; p = p->next)
        n++;
    if (n)
        warn("%d GC root pointers still exist on exit", n);

    n = 0;
    for (Root<Object> * p = frontRootSentinel->next; p != backRootSentinel; p = p->next)
        n++;
    if (n)
        warn("%d GC root objects still exist on exit", n);

    assert(!frontPtrSentinel->prev);
    assert(!backPtrSentinel->next);
    assert(!frontRootSentinel->prev);
    assert(!backRootSentinel->next);
}

void GC::addArena(size_t arenaSize)
{
    debug("allocating arena of %d bytes", arenaSize * WORD_SIZE);

    auto arena = Arena(arenaSize);

    // Add this arena to a freelist as a single block.
    addToFreeList(new (arena.start) Free(arenaSize));

    arenas.emplace_back(std::move(arena));

    totalSize += arenaSize;

    nextSize = arenaSize * 1.5; // FIXME: overflow, clamp
}

void GC::addToFreeList(Free * obj)
{
    auto size = obj->words();
    for (auto i = freeLists.rbegin(); i != freeLists.rend(); ++i)
        if (size >= i->minSize) {
            obj->next = i->front;
            i->front = obj;
            return;
        }
    abort();
}

void GC::gc()
{
    typedef std::chrono::time_point<std::chrono::steady_clock> steady_time_point;

    auto before = steady_time_point::clock::now();

    size_t marked = 0;

    std::stack<Object *> stack;

    // FIXME: ensure this gets inlined.
    auto push = [&](Object * p) { if (p) { assertObject(p); stack.push(p); } };

    auto pushPointers = [&](Object * obj) {
        switch (obj->type) {

        case tFree:
            printError("reached a freed object at %x", obj);
            abort();

        case tBindings: {
            auto obj2 = (Bindings *) obj;
            for (auto i = obj2->attrs; i < obj2->attrs + obj2->size_; ++i)
                push(i->value);
            break;
        }

        case tValueList: {
            auto obj2 = (PtrList<Object> *) obj;
            for (auto i = obj2->elems; i < obj2->elems + obj2->size(); ++i)
                push(*i);
            break;
        }

        case tEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            for (auto i = obj2->values; i < obj2->values + obj2->getSize(); ++i)
                push(*i);
            break;
        }

        case tWithExprEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            break;
        }

        case tWithAttrsEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            push(obj2->values[0]);
            break;
        }

        case tString:
        case tContext:
        case tInt:
        case tBool:
        case tNull:
        case tList0:
        case tFloat:
        case tShortString:
        case tStaticString:
            break;

        case tLongString: {
            auto obj2 = (Value *) obj;
            push(obj2->string.s);
            // See setContext().
            if (!(((ptrdiff_t) obj2->string.context) & 1))
                push(obj2->string.context);
            break;
        }

        case tPath:
            push(((Value *) obj)->path);
            break;

        case tAttrs:
            push(((Value *) obj)->attrs);
            break;

        case tList1:
            push(((Value *) obj)->smallList[0]);
            break;

        case tList2:
            push(((Value *) obj)->smallList[0]);
            push(((Value *) obj)->smallList[1]);
            break;

        case tListN:
            push(((Value *) obj)->bigList);
            break;

        case tThunk:
        case tBlackhole:
            push(((Value *) obj)->thunk.env);
            break;

        case tApp:
        case tPrimOpApp:
            push(((Value *) obj)->app.left);
            push(((Value *) obj)->app.right);
            break;

        case tLambda:
            push(((Value *) obj)->lambda.env);
            break;

        case tPrimOp:
            // FIXME: GC primops?
            break;

        default:
            printError("don't know how to traverse object at %x (tag %d)", obj, obj->type);
            abort();
        }
    };

    auto processStack = [&]() {
        while (!stack.empty()) {
            auto obj = stack.top();
            stack.pop();

            //printError("MARK %x", obj);

            if (!obj->isMarked()) {
                marked++;
                obj->mark();
                pushPointers(obj);
            }
        }
    };

    for (Root<Object> * p = frontRootSentinel->next; p != backRootSentinel; p = p->next) {
        pushPointers(&p->value);
        processStack();
    }

    for (Ptr<Object> * p = frontPtrSentinel->next; p != backPtrSentinel; p = p->next) {
        if (!p->value) continue;
        stack.push(p->value);
        processStack();
    }

    auto afterMark = steady_time_point::clock::now();

    // Reset all the freelists.
    for (auto & freeList : freeLists)
        freeList.front = nullptr;

    // Go through all the arenas and add free objects to the
    // appropriate freelists.
    size_t totalObjectsFreed = 0;
    size_t totalWordsFreed = 0;
    size_t totalObjectsKept = 0;
    size_t totalWordsKept = 0;

    for (auto & arena : arenas) {
        auto [objectsFreed, wordsFreed, objectsKept, wordsKept] = freeUnmarked(arena);
        totalObjectsFreed += objectsFreed;
        totalWordsFreed += wordsFreed;
        totalObjectsKept += objectsKept;
        totalWordsKept += wordsKept;
    }

    auto after = steady_time_point::clock::now();

    auto markDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(afterMark - before).count();
    auto sweepDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(after - afterMark).count();

    debug("freed %d dead objects (%d bytes), keeping %d/%d objects (%d bytes), marked in %d ms, swept in %d ms",
        totalObjectsFreed, totalWordsFreed * WORD_SIZE,
        marked, totalObjectsKept, totalWordsKept * WORD_SIZE,
        markDurationMs, sweepDurationMs);

    allTimeWordsFreed += totalWordsFreed;
    totalDurationMs += markDurationMs + sweepDurationMs;
}

size_t GC::getObjectSize(Object * obj)
{
    auto tag = obj->type;
    if (tag >= tInt && tag <= tFloat) {
        return ((Value *) obj)->words();
    } else {
        switch (tag) {
        case tFree:
            return ((Free *) obj)->words();
            break;
        case tString:
            return ((String *) obj)->words();
            break;
        case tBindings:
            return ((Bindings *) obj)->words();
            break;
        case tValueList:
            return ((PtrList<Value> *) obj)->words();
            break;
        case tEnv:
        case tWithExprEnv:
        case tWithAttrsEnv:
            return ((Env *) obj)->words();
            break;
        case tContext:
            return ((Context *) obj)->getSize() + 1;
            break;
        default:
            printError("GC encountered invalid object with tag %d", tag);
            abort();
        }
    }
}

std::tuple<size_t, size_t, size_t, size_t> GC::freeUnmarked(Arena & arena)
{
    size_t objectsFreed = 0;
    size_t wordsFreed = 0;
    size_t objectsKept = 0;
    size_t wordsKept = 0;

    auto end = arena.start + arena.size;
    auto pos = arena.start;

    Free * curFree = nullptr;

    auto linkCurFree = [&]() {
        if (curFree && curFree->words() > 1)
            addToFreeList(curFree);
        curFree = nullptr;
    };

    while (pos < end) {
        auto obj = (Object *) pos;

        auto objSize = getObjectSize(obj);

        // Merge current object into the previous free object.
        auto mergeFree = [&]() {
            //printError("MERGE %x %x %d", curFree, obj, curFree->size() + objSize);
            assert(curFree->words() >= 1);
            curFree->setSize(curFree->words() + objSize);
        };

        if (obj->type == tFree) {
            //debug("KEEP FREE %x %d", obj, obj->getMisc());
            if (curFree) {
                // Merge this object into the previous free
                // object.
                mergeFree();
            } else {
                curFree = (Free *) obj;
            }
        } else {

            if (obj->isMarked()) {
                // Unmark to prepare for the next GC run.
                //debug("KEEP OBJECT %x %d %d", obj, obj->type, objSize);
                linkCurFree();
                obj->unmark();
                objectsKept += 1;
                wordsKept += objSize;
            } else {
                //debug("FREE OBJECT %x %d %d", obj, obj->type, objSize);
                #if GC_DEBUG
                for (size_t i = 0; i < objSize; ++i)
                    ((Word *) obj)[i] = 0xdeadc0dedeadbeefULL;
                #endif
                objectsFreed += 1;
                wordsFreed += objSize;
                if (curFree) {
                    mergeFree();
                } else {
                    // Convert to a free object.
                    curFree = (Free *) obj;
                    curFree->type = tFree;
                    curFree->setSize(objSize);
                }
            }
        }

        pos += objSize;
    }

    linkCurFree();

    assert(pos == end);

    return {objectsFreed, wordsFreed, objectsKept, wordsKept};
}

bool GC::isObject(void * p)
{
    for (auto & arena : arenas)
        if (p >= arena.start && p < arena.start + arena.size)
            return true;
    return false;
}

std::tuple<size_t, size_t> GC::getObjectClosureSize(Object * p)
{
    std::unordered_set<Object *> seen;

    // FIXME: cut&paste.

    std::stack<Object *> stack;

    // FIXME: ensure this gets inlined.
    auto push = [&](Object * p) { if (p) { assertObject(p); stack.push(p); } };

    auto pushPointers = [&](Object * obj) {
        switch (obj->type) {

        case tFree:
            printError("reached a freed object at %x", obj);
            abort();

        case tBindings: {
            auto obj2 = (Bindings *) obj;
            for (auto i = obj2->attrs; i < obj2->attrs + obj2->size_; ++i)
                push(i->value);
            break;
        }

        case tValueList: {
            auto obj2 = (PtrList<Object> *) obj;
            for (auto i = obj2->elems; i < obj2->elems + obj2->size(); ++i)
                push(*i);
            break;
        }

        case tEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            for (auto i = obj2->values; i < obj2->values + obj2->getSize(); ++i)
                push(*i);
            break;
        }

        case tWithExprEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            break;
        }

        case tWithAttrsEnv: {
            auto obj2 = (Env *) obj;
            push(obj2->up);
            push(obj2->values[0]);
            break;
        }

        case tString:
        case tContext:
        case tInt:
        case tBool:
        case tNull:
        case tList0:
        case tFloat:
        case tShortString:
        case tStaticString:
            break;

        case tLongString: {
            auto obj2 = (Value *) obj;
            push(obj2->string.s);
            // See setContext().
            if (!(((ptrdiff_t) obj2->string.context) & 1))
                push(obj2->string.context);
            break;
        }

        case tPath:
            push(((Value *) obj)->path);
            break;

        case tAttrs:
            push(((Value *) obj)->attrs);
            break;

        case tList1:
            push(((Value *) obj)->smallList[0]);
            break;

        case tList2:
            push(((Value *) obj)->smallList[0]);
            push(((Value *) obj)->smallList[1]);
            break;

        case tListN:
            push(((Value *) obj)->bigList);
            break;

        case tThunk:
        case tBlackhole:
            push(((Value *) obj)->thunk.env);
            break;

        case tApp:
        case tPrimOpApp:
            push(((Value *) obj)->app.left);
            push(((Value *) obj)->app.right);
            break;

        case tLambda:
            push(((Value *) obj)->lambda.env);
            break;

        case tPrimOp:
            // FIXME: GC primops?
            break;

        default:
            printError("don't know how to traverse object at %x (tag %d)", obj, obj->type);
            abort();
        }
    };

    stack.push(p);

    size_t totalSize = 0;

    while (!stack.empty()) {
        auto obj = stack.top();
        stack.pop();
        if (seen.insert(obj).second) {
            pushPointers(obj);
            totalSize += getObjectSize(obj);
        }
    }

    return {seen.size(), totalSize};
}

}
