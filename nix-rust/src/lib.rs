#![feature(await_macro, async_await)]

mod binary_cache_store;
mod error;
mod foreign;
mod store;
mod tarfile;
mod path_info;

pub use error::Error;

pub struct CBox<T> {
    pub ptr: *mut libc::c_void,
    phantom: std::marker::PhantomData<T>,
}

impl<T> CBox<T> {
    fn new(t: T) -> Self {
        unsafe {
            let size = std::mem::size_of::<T>();
            let ptr = libc::malloc(size);
            *(ptr as *mut T) = t; // FIXME: probably UB
            Self {
                ptr,
                phantom: std::marker::PhantomData,
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn unpack_tarfile(
    source: foreign::Source,
    dest_dir: &str,
) -> CBox<Result<(), error::CppException>> {
    CBox::new(tarfile::unpack_tarfile(source, dest_dir).map_err(|err| err.into()))
}

#[no_mangle]
pub extern "C" fn rust_test() {
    use crate::store::Store;
    use futures::future::{FutureExt, TryFutureExt};
    use std::path::Path;

    let fut = async move {
        let store: Box<dyn Store> = Box::new(binary_cache_store::BinaryCacheStore::new(
            "https://cache.nixos.org".to_string(),
        ));

        let path = store
            .parse_store_path(&Path::new(
                "/nix/store/7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-konsole-18.12.3",
            ))
            .unwrap();

        /*
        let info = store.query_path_info(&path).await.unwrap();

        eprintln!("INFO = {:?}", info);
         */

        let closure = store.compute_path_closure(vec![path].into_iter().collect()).await.unwrap();

        eprintln!("CLOSURE = {:?}", closure.len());

        Ok(())
    };

    tokio::run(fut.boxed().compat());
}
