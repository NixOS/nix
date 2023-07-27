#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

// forward declaration
namespace nix {
class EvalState;
class BindingsBuilder;
}; // namespace nix

struct State {
  nix::EvalState state;
};

struct GCRef {
  void *ptr;
};

struct BindingsBuilder {
  nix::BindingsBuilder builder;
};

#endif // NIX_API_EXPR_INTERNAL_H
