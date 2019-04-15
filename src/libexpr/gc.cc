#include "gc.hh"
#include "value.hh"
#include "attr-set.hh"
#include "eval.hh"

namespace nix {

GC gc;

GC::GC()
{
    // FIXME: placement new
    frontSentinel = (Ptr<Object> *) malloc(sizeof(Ptr<Object>));
    backSentinel = (Ptr<Object> *) malloc(sizeof(Ptr<Object>));

    frontSentinel->prev = nullptr;
    frontSentinel->next = backSentinel;

    backSentinel->prev = frontSentinel;
    backSentinel->next = nullptr;
}

GC::~GC()
{
    size_t n = 0;
    for (Ptr<Object> * p = frontSentinel->next; p != backSentinel; p = p->next)
        n++;
    if (n)
        warn("%d GC roots still exist on exit", n);
    assert(!frontSentinel->prev);
    assert(!backSentinel->next);
}

void GC::gc()
{
    size_t marked = 0;

    std::stack<Object *> stack;

    for (Ptr<Object> * p = frontSentinel->next; p != backSentinel; p = p->next) {
        if (!p->value) continue;

        stack.push(p->value);

        while (!stack.empty()) {
            auto obj = stack.top();
            stack.pop();

            // FIXME: ensure this gets inlined.
            auto push = [&](Object * p) { if (p) { /* FIXME */ assert(isObject(p)); stack.push(p); } };

            //printError("MARK %x", obj);

            if (!obj->isMarked()) {
                marked++;
                obj->mark();
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
                    if (obj2->type != Env::HasWithExpr)
                        for (auto i = obj2->values; i < obj2->values + obj2->size; ++i)
                            push(*i);
                    break;
                }

                case tInt:
                case tBool:
                case tNull:
                case tList0:
                case tFloat:
                    break;

                case tString:
                    // FIXME
                    break;

                case tPath:
                    // FIXME
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

                case tBlackhole:
                    // FIXME
                    break;

                case tPrimOp:
                    // FIXME: GC primops?
                    break;

                default:
                    printError("don't know how to traverse object at %x (tag %d)", obj, obj->type);
                    abort();
                }
            }
        }
    }

    Size totalObjectsFreed = 0;
    Size totalWordsFreed = 0;

    for (auto & arenaList : arenaLists) {

        for (auto & arena : arenaList.arenas) {
            auto [objectsFreed, wordsFreed] = arena.freeUnmarked();
            totalObjectsFreed += objectsFreed;
            totalWordsFreed += wordsFreed;
        }

        std::sort(arenaList.arenas.begin(), arenaList.arenas.end(),
            [](const Arena & a, const Arena & b) {
                return b.free < a.free;
            });
    }

    printError("freed %d bytes in %d dead objects, keeping %d objects",
        totalWordsFreed * WORD_SIZE, totalObjectsFreed, marked);
}

std::pair<Size, Size> GC::Arena::freeUnmarked()
{
    Size objectsFreed = 0;
    Size wordsFreed = 0;

    auto end = start + size;
    auto pos = start;

    Free * curFree = nullptr;
    Free * * freeLink = &firstFree;

    free = 0;

    while (pos < end) {
        auto obj = (Object *) pos;
        auto tag = obj->type;

        auto linkFree = [&]() {
            *freeLink = curFree;
            freeLink = &curFree->next;
        };

        Size objSize;
        if (tag >= tInt && tag <= tFloat) {
            objSize = ((Value *) obj)->words();
        } else {
            switch (tag) {
            case tFree:
                objSize = ((Free *) obj)->words();
                break;
            case tBindings:
                objSize = ((Bindings *) obj)->words();
                break;
            case tValueList:
                objSize = ((PtrList<Value> *) obj)->words();
                break;
            case tEnv:
                objSize = ((Env *) obj)->words();
                break;
            default:
                abort();
                //throw Error("GC encountered invalid object with tag %d", tag);
            }
        }

        // Merge current object into the previous free object.
        auto mergeFree = [&]() {
            //printError("MERGE %x %x %d", curFree, obj, curFree->size() + objSize);
            assert(curFree->words() >= 1);
            if (curFree->words() == 1) {
                linkFree();
                free += 1;
            }
            curFree->setSize(curFree->words() + objSize);
            free += objSize;
        };

        if (tag == tFree) {
            if (curFree) {
                // Merge this object into the previous free
                // object.
                mergeFree();
            } else {
                curFree = (Free *) obj;
                if (curFree->words() > 1) {
                    linkFree();
                    free += curFree->words();
                }
            }
        } else {

            if (obj->isMarked()) {
                // Unmark to prepare for the next GC run.
                curFree = nullptr;
                obj->unmark();
            } else {
                //printError("FREE %x %d %d", obj, obj->type, objSize);
                objectsFreed += 1;
                wordsFreed += objSize;
                if (curFree) {
                    mergeFree();
                } else {
                    // Convert to a free object.
                    curFree = (Free *) obj;
                    curFree->type = tFree;
                    curFree->setSize(objSize);
                    linkFree();
                    free += objSize;
                }
            }
        }

        pos += objSize;
    }

    assert(pos == end);

    *freeLink = nullptr;

    return {objectsFreed, wordsFreed};
}

bool GC::isObject(void * p)
{
    for (auto & arenaList : arenaLists) {
        for (auto & arena : arenaList.arenas) {
            if (p >= arena.start && p < arena.start + arena.size)
                return true;
        }
    }
    return false;
}

GC::ArenaList::ArenaList()
    : nextSize(1024)
{
}

}
