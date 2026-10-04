#pragma once
#include "nix/expr/eval-profiler.hh"
#include <boost/unordered/concurrent_flat_map.hpp>
#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <fstream>
#include <stdexcept>
#include <sys/mman.h>
#include <cassert>
#include "nix/expr/eval-profiler.hh"
#include "nix/expr/eval.hh"
using std::ofstream;
using std::format;
namespace nix {
struct Env;
struct Expr;
class EvalState;
struct ExprLambda;
struct Value;

// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" void * _Perf_frame_func_start; // from asm
// NOLINTNEXTLINE(bugprone-reserved-identifier)
extern "C" void * _Perf_frame_func_end; // from asm

extern "C" void *_Perf_frame_func_start; // start of the template of the perf trampoline
extern "C" void *_Perf_frame_func_end; // start of the template of the perf trampoline

using PerfShim = void (*)( EvalState & state, Env & env, Value & v,ExprLambda & lambda, std::exception_ptr & e);
using PerfFrame = void (*)( EvalState & state, Env & env, Value & v,ExprLambda & lambda, std::exception_ptr & e, PerfShim  );
void perf_shim( EvalState & state, Env & env, Value & v,ExprLambda & lambda, std::exception_ptr & e)
{
    try { lambda.body->eval(state,env,v); }
    catch (...) { e = std::current_exception(); }
}
// since our trampoline does not have debug info this shim exists to catch errors and hoist them through the trampoline to be rethrown
// the trampoline store creates a new executable trampoline frame and writes its name to /tmp/perf-{pid}.map
// it is backed by is a non freeing linear allocator
// When empty it requests a new large block of memory and prefills it
// if freeing frame info is required jitdump format should be used
class PerfTrampolineStore{
    // ingores past trampolines, exp scaling would mean unpredictable pauses and wasted space
    // 32 bit alignment was copied from pythons implementation
    // it conservatively fits abi alignment
public:
    const size_t ALLOCATION_SIZE = 256*1024; // 256kb per arena safely bigger then a page ~16k frames
    const size_t ALIGNMENT = 4;
    const void* start = &_Perf_frame_func_start;
    const void* end = &_Perf_frame_func_end;
    const size_t CODE_SIZE =  (char*)end - (char*)start;
    // round up to alignment
    const size_t TRAMPOLINE_SIZE = ((CODE_SIZE + (ALIGNMENT - 1))/ALIGNMENT)*ALIGNMENT;
    const size_t TRAMPOLINES_PER_ALLOC = ALLOCATION_SIZE / TRAMPOLINE_SIZE;


     PerfFrame get_trampoline(ExprLambda& lambda,const EvalState& state) {
        if (trampoline_map.empty()) {
            trampoline_map.reserve(4096);
        }
        PerfFrame frame;
        bool found = trampoline_map.cvisit(&lambda, [&](const auto& x){frame=x.second;});
        if (found) { return frame;} else
        {
            PerfFrame f = new_func(lambda.showNamePos(state));
            trampoline_map.insert(std::pair<ExprLambda*,PerfFrame>(&lambda,f));
            frame = f;
        }
        return frame;
    }
private:
    PerfFrame new_frame() {
        assert(trampolines_left >= 0);
        assert(TRAMPOLINE_SIZE % ALIGNMENT == 0);
        if (!trampolines_left) {
            // fill a new memory region with frames when we run out of space
            current = mmap(NULL, // adress hint
                           ALLOCATION_SIZE, PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,
                           -1, NULL); // fd & offset (unused)
            if (current == MAP_FAILED) {
                /// TODO: return result properly
                throw std::runtime_error("perf map: mmap failed");
            }
            // fill allocated block with trampolines
            for (size_t i = 0; i < TRAMPOLINES_PER_ALLOC; i++) {
                memcpy((char*) current + i * TRAMPOLINE_SIZE, start, CODE_SIZE);
            }
            // memory should not be both write and execute some systems may forbid this
            int err = mprotect(current,ALLOCATION_SIZE, PROT_READ | PROT_EXEC);
            if (err != 0) { throw std::runtime_error("perf map: mprotect failled"); }
            trampolines_left = TRAMPOLINES_PER_ALLOC;
        } else {
            current = (char *) current + TRAMPOLINE_SIZE;
        }
        trampolines_left--;
        assert(current != nullptr);
        return (PerfFrame) current;
    }
    PerfFrame new_func(std::string name) {
        if (file.fail()) {
            throw std::runtime_error("perf map: mapfile failed to open");
        }
       PerfFrame frame = new_frame();
        // perf format is <adress> <size> <name>
       auto perf_map_entry = std::format("{:x} {:x} {}\n",(size_t) frame,CODE_SIZE,name);
       file << perf_map_entry;
       return frame;
    }
    ofstream file = ofstream(format("/tmp/perf-{}.map",getpid()));
    void* current = nullptr;
    size_t trampolines_left = 0;
    boost::concurrent_flat_map<ExprLambda*, PerfFrame> trampoline_map;

};
PerfTrampolineStore * makePerfTrampolineStore() {return new PerfTrampolineStore();}

inline void callTrampoline(PerfTrampolineStore * perfStore, EvalState & state, Env & env, Value & v,ExprLambda & lambda) {
    std::exception_ptr e;
    PerfFrame f = perfStore->get_trampoline(lambda,state);
    (f)(state,env,v,lambda,e, ::nix::perf_shim);
    if (e) {
        std::rethrow_exception(e);
    }
}
}
