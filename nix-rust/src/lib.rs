#![feature(await_macro, async_await)]

#[macro_use]
extern crate lazy_static;

#[cfg(test)]
#[macro_use]
extern crate assert_matches;

#[cfg(test)]
#[macro_use]
extern crate proptest;

mod c;
mod error;
mod foreign;
mod store;
mod util;

pub use error::Error;
