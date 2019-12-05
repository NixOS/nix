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
        if base_name.len() < STORE_PATH_HASH_CHARS + 1
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
        if s.len() == 0 {
            return Err(Error::StorePathNameEmpty);
        }

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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-konsole-18.12.3";
        let p = StorePath::new_from_base_name(&s).unwrap();
        assert_eq!(p.name.0, "konsole-18.12.3");
        assert_eq!(
            p.hash.0,
            [
                0x9f, 0x76, 0x49, 0x20, 0xf6, 0x5d, 0xe9, 0x71, 0xc4, 0xca, 0x46, 0x21, 0xab, 0xff,
                0x9b, 0x44, 0xef, 0x87, 0x0f, 0x3c
            ]
        );
    }

    #[test]
    fn test_no_name() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::StorePathNameEmpty)
        );
    }

    #[test]
    fn test_no_dash() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::BadStorePath(_))
        );
    }

    #[test]
    fn test_short_hash() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxl-konsole-18.12.3";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::BadStorePath(_))
        );
    }

    #[test]
    fn test_invalid_hash() {
        let s = "7h7qgvs4kgzsn8e6rb273saxyqh4jxlz-konsole-18.12.3";
        assert_matches!(StorePath::new_from_base_name(&s), Err(Error::BadBase32));
    }

    #[test]
    fn test_long_name() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        assert_matches!(StorePath::new_from_base_name(&s), Ok(_));
    }

    #[test]
    fn test_too_long_name() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::StorePathNameTooLong)
        );
    }

    #[test]
    fn test_bad_name() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-foo bar";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::BadStorePathName)
        );

        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-kónsole";
        assert_matches!(
            StorePath::new_from_base_name(&s),
            Err(Error::BadStorePathName)
        );
    }

    #[test]
    fn test_roundtrip() {
        let s = "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz-konsole-18.12.3";
        assert_eq!(StorePath::new_from_base_name(&s).unwrap().to_string(), s);
    }
}
