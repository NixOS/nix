use std::fmt;

#[derive(Debug)]
pub enum Error {
    InvalidPath(crate::store::StorePath),
    BadStorePath(std::path::PathBuf),
    BadNarInfo,
    BadBase32,
    StorePathNameTooLong,
    BadStorePathName,
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

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidPath(_) => write!(f, "invalid path"),
            Error::BadNarInfo => write!(f, ".narinfo file is corrupt"),
            Error::BadStorePath(path) => write!(f, "path '{}' is not a store path", path.display()),
            Error::BadBase32 => write!(f, "invalid base32 string"),
            Error::StorePathNameTooLong => {
                write!(f, "store path name is longer than 211 characters")
            }
            Error::BadStorePathName => write!(f, "store path name contains forbidden character"),
            Error::IOError(err) => write!(f, "I/O error: {}", err),
            Error::HttpError(err) => write!(f, "HTTP error: {}", err),
            Error::Foreign(_) => write!(f, "<C++ exception>"), // FIXME
            Error::Misc(s) => write!(f, "{}", s),
        }
    }
}

impl From<Error> for CppException {
    fn from(err: Error) -> Self {
        match err {
            Error::Foreign(ex) => ex,
            _ => unsafe { make_error(&err.to_string()) },
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
