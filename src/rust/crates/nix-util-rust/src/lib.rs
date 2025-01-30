// Clippy lints enabled globally
#![deny(clippy::all)]
#![deny(clippy::cargo)]
#![deny(clippy::nursery)]
#![deny(clippy::pedantic)]
#![deny(clippy::restriction)]
#![deny(clippy::implicit_return)]
// Clippy lints disabled globally
#![allow(clippy::absolute_paths, reason = "style")]
#![allow(clippy::arbitrary_source_item_ordering, reason = "style")]
#![allow(clippy::blanket_clippy_restriction_lints, reason = "style")]
#![allow(clippy::mem_forget, reason = "used by no_panic")]
// #![allow(clippy::missing_docs_in_private_items)]
#![allow(clippy::module_name_repetitions, reason = "style")]
#![allow(clippy::needless_return, reason = "style")]
#![allow(clippy::question_mark_used, reason = "style")]

//! This crate defines the nix-util-rust cxxbridge.

/// Generic error type
type BoxError = Box<dyn core::error::Error + Send + Sync + 'static>;
/// Generic error-result type
type BoxResult<T> = Result<T, BoxError>;

/// Implementations for the cxx FFI items.
pub mod ffi {
    pub mod hash {
        pub mod blake3;
    }
}

/// Declarations for generating the cxx bridge code.
pub mod gen {
    // Clippy lints disabled for cxx generated code.
    #![allow(
        clippy::implicit_return,
        reason = "override lints for cxx generated code"
    )]
    #![allow(
        clippy::multiple_unsafe_ops_per_block,
        reason = "override lints for cxx generated code"
    )]
    #![allow(
        clippy::trait_duplication_in_bounds,
        reason = "override lints for cxx generated code"
    )]

    pub mod hash {
        pub mod blake3;
    }
}
