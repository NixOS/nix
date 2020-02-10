//! Error types

use std::fmt;

/// An error that can occur in this crate.
#[derive(Debug)]
pub enum Error {
    /// The store path could not be resolved.
    InvalidPath(crate::store::StorePath),
    /// The store path is in the wrong format.
    BadStorePath(std::path::PathBuf),
    /// The store path does not exist in the store.
    NotInStore(std::path::PathBuf),
    /// The NAR info is in the wrong format.
    BadNarInfo,
    /// Base32 string is invalid.
    BadBase32,
    /// The store path name is empty.
    StorePathNameEmpty,
    /// The store path name is longer than 211 characters.
    StorePathNameTooLong,
    /// The store path name contains unexpected characters.
    BadStorePathName,
    /// The NAR size field is too big.
    NarSizeFieldTooBig,
    /// The NAR string is not valid UTF-8.
    BadNarString,
    /// The NAR padding contains bytes other than '0'.
    BadNarPadding,
    /// The NAR version magic string has an unexpected value.
    BadNarVersionMagic,
    /// An opening parenthesis was expected in the NAR file, but not found.
    MissingNarOpenTag,
    /// A closing parenthesis was expected in the NAR file, but not found.
    MissingNarCloseTag,
    /// A field was expected in the NAR file, but it is missing.
    MissingNarField,
    /// An unexpected NAR field was encountered.
    BadNarField(String),
    /// The 'executable' field is non-empty.
    BadExecutableField,
    /// An I/O error occurred.
    IOError(std::io::Error),
    /// An HTTP error occurred.
    #[cfg(unused)]
    HttpError(hyper::error::Error),
    /// An uncategorized error occurred.
    Misc(String),
    /// A C++ exception occurred.
    #[cfg(not(test))]
    Foreign(CppException),
    /// The tar file member name is invalid.
    BadTarFileMemberName(String),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        Error::IOError(err)
    }
}

#[cfg(unused)]
impl From<hyper::error::Error> for Error {
    fn from(err: hyper::error::Error) -> Self {
        Error::HttpError(err)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidPath(_) => write!(f, "invalid path"),
            Error::BadNarInfo => write!(f, ".narinfo file is corrupt"),
            Error::BadStorePath(path) => write!(f, "path '{}' is not a store path", path.display()),
            Error::NotInStore(path) => {
                write!(f, "path '{}' is not in the Nix store", path.display())
            }
            Error::BadBase32 => write!(f, "invalid base32 string"),
            Error::StorePathNameEmpty => write!(f, "store path name is empty"),
            Error::StorePathNameTooLong => {
                write!(f, "store path name is longer than 211 characters")
            }
            Error::BadStorePathName => write!(f, "store path name contains forbidden character"),
            Error::NarSizeFieldTooBig => write!(f, "size field in NAR is too big"),
            Error::BadNarString => write!(f, "NAR string is not valid UTF-8"),
            Error::BadNarPadding => write!(f, "NAR padding is not zero"),
            Error::BadNarVersionMagic => write!(f, "unsupported NAR version"),
            Error::MissingNarOpenTag => write!(f, "NAR open tag is missing"),
            Error::MissingNarCloseTag => write!(f, "NAR close tag is missing"),
            Error::MissingNarField => write!(f, "expected NAR field is missing"),
            Error::BadNarField(s) => write!(f, "unrecognized NAR field '{}'", s),
            Error::BadExecutableField => write!(f, "bad 'executable' field in NAR"),
            Error::IOError(err) => write!(f, "I/O error: {}", err),
            #[cfg(unused)]
            Error::HttpError(err) => write!(f, "HTTP error: {}", err),
            #[cfg(not(test))]
            Error::Foreign(_) => write!(f, "<C++ exception>"), // FIXME
            Error::Misc(s) => write!(f, "{}", s),
            Error::BadTarFileMemberName(s) => {
                write!(f, "tar archive contains illegal file name '{}'", s)
            }
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::IOError(err) => Some(err),
            #[cfg(unused)]
            Error::HttpError(err) => Some(err),
            #[cfg(not(test))]
            Error::Foreign(_) => None, // Should the underlying error be considered a "source" in the Rust error sense?
            Error::InvalidPath(_)
            | Error::BadNarInfo
            | Error::BadStorePath(_)
            | Error::NotInStore(_)
            | Error::BadBase32
            | Error::StorePathNameEmpty
            | Error::StorePathNameTooLong
            | Error::BadStorePathName
            | Error::NarSizeFieldTooBig
            | Error::BadNarString
            | Error::BadNarPadding
            | Error::BadNarVersionMagic
            | Error::MissingNarOpenTag
            | Error::MissingNarCloseTag
            | Error::MissingNarField
            | Error::BadNarField(_)
            | Error::BadExecutableField
            | Error::Misc(_)
            | Error::BadTarFileMemberName(_) => None,
        }
    }
}

#[cfg(not(test))]
impl From<Error> for CppException {
    fn from(err: Error) -> Self {
        match err {
            Error::Foreign(ex) => ex,
            _ => CppException::new(&err.to_string()),
        }
    }
}

#[cfg(not(test))]
#[repr(C)]
#[derive(Debug)]
pub struct CppException(*const libc::c_void); // == std::exception_ptr*

#[cfg(not(test))]
impl CppException {
    fn new(s: &str) -> Self {
        Self(unsafe { make_error(s) })
    }
}

#[cfg(not(test))]
impl Drop for CppException {
    fn drop(&mut self) {
        unsafe {
            destroy_error(self.0);
        }
    }
}

#[cfg(not(test))]
extern "C" {
    #[allow(improper_ctypes)] // YOLO
    fn make_error(s: &str) -> *const libc::c_void;

    fn destroy_error(exc: *const libc::c_void);
}
