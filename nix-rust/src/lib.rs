mod error;
mod foreign;
mod tarfile;

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
