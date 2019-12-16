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
