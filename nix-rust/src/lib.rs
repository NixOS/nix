mod error;
mod foreign;
mod tarfile;

pub use error::Error;

#[no_mangle]
pub extern "C" fn unpack_tarfile(source: foreign::Source, dest_dir: &str) {
    tarfile::unpack_tarfile(source, dest_dir).unwrap();
}
