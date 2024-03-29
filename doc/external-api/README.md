# Getting started

> **Warning** These bindings are **experimental**, which means they can change
> at any time or be removed outright; nevertheless the plan is to provide a
> stable external C API to the Nix language and the Nix store.

The language library allows evaluating Nix expressions and interacting with Nix
language values. The Nix store API is still rudimentary, and only allows
initialising and connecting to a store for the Nix language evaluator to
interact with.

Currently there are two ways to interface with the Nix language evaluator
programmatically:

1. Embedding the evaluator
2. Writing language plug-ins

Embedding means you link the Nix C libraries in your program and use them from
there. Adding a plug-in means you make a library that gets loaded by the Nix
language evaluator, specified through a configuration option.

Many of the components and mechanisms involved are not yet documented, therefore
please refer to the [Nix source code](https://github.com/NixOS/nix/) for
details. Additions to in-code documentation and the reference manual are highly
appreciated.

The following examples, for simplicity, don't include error handling. See the
[Handling errors](@ref errors) section for more information.

# Embedding the Nix Evaluator

In this example we programmatically start the Nix language evaluator with a
dummy store (that has no store paths and cannot be written to), and evaluate the
Nix expression `builtins.nixVersion`.

**main.c:**

```C
#include <nix_api_util.h>
#include <nix_api_expr.h>
#include <nix_api_value.h>
#include <stdio.h>

// NOTE: This example lacks all error handling. Production code must check for
// errors, as some return values will be undefined.
int main() {
   nix_libexpr_init(NULL);

   Store* store = nix_store_open(NULL, "dummy://", NULL);
   EvalState* state = nix_state_create(NULL, NULL, store); // empty search path (NIX_PATH)
   Value *value = nix_alloc_value(NULL, state);

   nix_expr_eval_from_string(NULL, state, "builtins.nixVersion", ".", value);
   nix_value_force(NULL, state, value);
   printf("Nix version: %s\n", nix_get_string(NULL, value));

   nix_gc_decref(NULL, value);
   nix_state_free(state);
   nix_store_free(store);
   return 0;
}
```

**Usage:**

```ShellSession
$ gcc main.c $(pkg-config nix-expr-c --libs --cflags) -o main
$ ./main
Nix version: 2.17
```

# Writing a Nix language plug-in

In this example we add a custom primitive operation (_primop_) to `builtins`. It
will increment the argument if it is an integer and throw an error otherwise.

**plugin.c:**

```C
#include <nix_api_util.h>
#include <nix_api_expr.h>
#include <nix_api_value.h>

void increment(void* user_data, nix_c_context* ctx, EvalState* state, Value** args, Value* v) {
    nix_value_force(NULL, state, args[0]);
    if (nix_get_type(NULL, args[0]) == NIX_TYPE_INT) {
      nix_init_int(NULL, v, nix_get_int(NULL, args[0]) + 1);
    } else {
      nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "First argument should be an integer.");
    }
}

void nix_plugin_entry() {
  const char* args[] = {"n", NULL};
  PrimOp *p = nix_alloc_primop(NULL, increment, 1, "increment", args, "Example custom built-in function: increments an integer", NULL);
  nix_register_primop(NULL, p);
  nix_gc_decref(NULL, p);
}
```

**Usage:**

```ShellSession
$ gcc plugin.c $(pkg-config nix-expr-c --libs --cflags) -shared -o plugin.so
$ nix --plugin-files ./plugin.so repl
nix-repl> builtins.increment 1
2
```
