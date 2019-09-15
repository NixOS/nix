/// A wrapper around Nix's Source class that provides the Read trait.
#[repr(C)]
pub struct Source {
    fun: extern "C" fn(this: *mut libc::c_void, data: &mut [u8]) -> usize,
    this: *mut libc::c_void,
}

impl std::io::Read for Source {
    fn read(&mut self, buf: &mut [u8]) -> std::result::Result<usize, std::io::Error> {
        let n = (self.fun)(self.this, buf);
        assert!(n <= buf.len());
        Ok(n)
    }
}

pub struct CBox<T> {
    pub ptr: *mut libc::c_void,
    phantom: std::marker::PhantomData<T>,
}

impl<T> CBox<T> {
    pub fn new(t: T) -> Self {
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
