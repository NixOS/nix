# Getting started

There are two ways to interface with nix: embedding it, or as a plugin. Embedding means you link one of the nix libraries in your program and use it from there, while being a plugin means you make a library that gets loaded by the nix evaluator, specified through a configuration option.

# Embedding the Nix Evaluator

These examples don't include error handling.
See the [Handling errors](@ref errors) section for more information.

**main.c:**
```C
#include <nix_api_util.h>
#include <nix_api_expr.h>
#include <nix_api_value.h>
#include <stdio.h>

int main() {
   nix_libexpr_init(NULL);

   Store* store = nix_store_open(NULL, "dummy://", NULL);
   State* state = nix_state_create(NULL, NULL, store); // empty nix path
   Value *value = nix_alloc_value(NULL, state);

   nix_expr_eval_from_string(NULL, state, "builtins.nixVersion", ".", value);
   nix_value_force(NULL, state, value);
   printf("nix version: %s\n", nix_get_string(NULL, value));

   nix_gc_decref(NULL, value);
   nix_state_free(state);
   nix_store_unref(store);
   return 0;
}
```
 
**Usage:**
```
$ gcc main.c $(pkg-config nix-expr-c --libs --cflags) -o main
$ ./main
nix version 1.2.3
```


# Writing a Nix Plugin

**plugin.c:**
```C
#include <nix_api_util.h>
#include <nix_api_expr.h>
#include <nix_api_value.h>
 
void increment(State* state, int pos, Value** args, Value* v) {
    nix_value_force(NULL, state, args[0]);
    if (nix_get_type(NULL, args[0]) == NIX_TYPE_INT) {
      nix_set_int(NULL, v, nix_get_int(NULL, args[0]) + 1);
    } else {
      nix_set_null(NULL, v);
    }
}
 
void nix_plugin_entry() {
  const char* args[] = {"n", NULL};
  PrimOp *p = nix_alloc_primop(NULL, increment, 1, "increment", args, "Example nix plugin function: increments an int");
  nix_register_primop(NULL, p);
  nix_gc_decref(NULL, p);
}
```

**Usage:**
```
$ gcc plugin.c $(pkg-config nix-expr-c --libs --cflags) -shared -o plugin.so
$ nix --plugin-files ./plugin.so repl
nix-repl> builtins.increment 1
2
```
