#[derive(Debug)]
pub enum Error {
    IOError(std::io::Error),
    Misc(String),
    Foreign(CppException),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        Error::IOError(err)
    }
}

impl From<Error> for CppException {
    fn from(err: Error) -> Self {
        match err {
            Error::Foreign(ex) => ex,
            Error::Misc(s) => unsafe { make_error(&s) },
            Error::IOError(err) => unsafe { make_error(&err.to_string()) },
        }
    }
}

#[repr(C)]
#[derive(Debug)]
pub struct CppException(*const libc::c_void); // == std::exception_ptr*

extern "C" {
    #[allow(improper_ctypes)] // YOLO
    fn make_error(s: &str) -> CppException;
}
