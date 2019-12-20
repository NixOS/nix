#[macro_use]
extern crate lazy_static;

#[cfg(test)]
#[macro_use]
extern crate assert_matches;

#[cfg(test)]
#[macro_use]
extern crate proptest;

#[cfg(not(test))]
mod c;
mod error;
#[cfg(unused)]
mod nar;
mod store;
mod util;

pub use error::Error;
