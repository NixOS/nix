use crate::error::Error;
use crate::util::base32;
use std::fmt;
use std::path::Path;

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct StorePath {
    pub hash: StorePathHash,
    pub name: StorePathName,
}

pub const STORE_PATH_HASH_BYTES: usize = 20;
pub const STORE_PATH_HASH_CHARS: usize = 32;

impl StorePath {
    pub fn new(path: &Path, _store_dir: &str) -> Result<Self, Error> {
        // FIXME: check store_dir
        Self::new_from_base_name(
            path.file_name()
                .ok_or(Error::BadStorePath(path.into()))?
                .to_str()
                .ok_or(Error::BadStorePath(path.into()))?,
        )
    }

    pub fn new_from_base_name(base_name: &str) -> Result<Self, Error> {
        if base_name.len() < STORE_PATH_HASH_CHARS + 2
            || base_name.as_bytes()[STORE_PATH_HASH_CHARS] != '-' as u8
        {
            return Err(Error::BadStorePath(base_name.into()));
        }

        Ok(StorePath {
            hash: StorePathHash::new(&base_name[0..STORE_PATH_HASH_CHARS])?,
            name: StorePathName::new(&base_name[STORE_PATH_HASH_CHARS + 1..])?,
        })
    }
}

impl fmt::Display for StorePath {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}-{}", self.hash, self.name)
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct StorePathHash([u8; STORE_PATH_HASH_BYTES]);

impl StorePathHash {
    pub fn new(s: &str) -> Result<Self, Error> {
        assert_eq!(s.len(), STORE_PATH_HASH_CHARS);
        let v = base32::decode(s)?;
        assert_eq!(v.len(), STORE_PATH_HASH_BYTES);
        let mut bytes: [u8; 20] = Default::default();
        bytes.copy_from_slice(&v[0..STORE_PATH_HASH_BYTES]);
        Ok(Self(bytes))
    }
}

impl fmt::Display for StorePathHash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&base32::encode(&self.0))
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct StorePathName(String);

impl StorePathName {
    pub fn new(s: &str) -> Result<Self, Error> {
        if s.len() > 211 {
            return Err(Error::StorePathNameTooLong);
        }

        if s.starts_with('.')
            || !s.chars().all(|c| {
                c.is_ascii_alphabetic()
                    || c.is_ascii_digit()
                    || c == '+'
                    || c == '-'
                    || c == '.'
                    || c == '_'
                    || c == '?'
                    || c == '='
            })
        {
            return Err(Error::BadStorePathName);
        }

        Ok(Self(s.to_string()))
    }
}

impl fmt::Display for StorePathName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

// FIXME: add tests
