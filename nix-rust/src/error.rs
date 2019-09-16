#[derive(Debug)]
pub enum Error {
    InvalidPath(crate::store::StorePath),
    BadStorePath(std::path::PathBuf),
    BadNarInfo,
    BadBase32,
    IOError(std::io::Error),
    HttpError(reqwest::Error),
    Misc(String),
    Foreign(CppException),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        Error::IOError(err)
    }
}

impl From<reqwest::Error> for Error {
    fn from(err: reqwest::Error) -> Self {
        Error::HttpError(err)
    }
}

impl From<Error> for CppException {
    fn from(err: Error) -> Self {
        match err {
            Error::InvalidPath(_) => unsafe { make_error("invalid path") }, // FIXME
            Error::BadNarInfo => unsafe { make_error(".narinfo file is corrupt") }, // FIXME
            Error::BadStorePath(path) => unsafe {
                make_error(&format!("path '{}' is not a store path", path.display()))
            }, // FIXME
            Error::BadBase32 => unsafe { make_error("invalid base32 string") }, // FIXME
            Error::IOError(err) => unsafe { make_error(&err.to_string()) },
            Error::HttpError(err) => unsafe { make_error(&err.to_string()) },
            Error::Foreign(ex) => ex,
            Error::Misc(s) => unsafe { make_error(&s) },
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
