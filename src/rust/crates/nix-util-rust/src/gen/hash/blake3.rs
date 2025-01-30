use crate::ffi::hash::blake3::{clone, create, finalize, update, update_mmap, Blake3Ctx};

#[cxx::bridge]
pub mod ffi {
    #[namespace = "nix::rust::hash::blake3"]
    extern "Rust" {
        pub type Blake3Ctx;

        pub fn clone(ctx: &Blake3Ctx) -> Box<Blake3Ctx>;

        pub fn create() -> Box<Blake3Ctx>;

        pub fn update(ctx: &mut Blake3Ctx, slice: &[u8]);

        pub fn update_mmap(ctx: &mut Blake3Ctx, path: &str, hint: usize) -> Result<()>;

        pub fn finalize(slice: &mut [u8], ctx: &Blake3Ctx) -> Result<()>;
    }
}
