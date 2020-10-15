#[allow(improper_ctypes_definitions)]
#[cfg(not(test))]
mod c;
mod error;
#[cfg(unused)]
mod nar;
mod store;
mod util;

pub use error::Error;
