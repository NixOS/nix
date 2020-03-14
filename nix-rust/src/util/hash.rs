use crate::util::base16;
use crate::util::base32;
use crate::util::base64;

use Base::*;
use Hash::*;
use HashType::*;

#[derive(Copy, Clone, PartialEq, PartialOrd, Eq, Ord)]
#[repr(u8)]
pub enum HashType {
    UNKNOWN,
    MD5,
    SHA1,
    SHA256,
    SHA512,
}

impl HashType {
    fn print_hash_type(self) -> &'static str {
        match self {
            UNKNOWN => panic!(),
            MD5 => "msd5",
            SHA1 => "sha1",
            SHA256 => "sha256",
            SHA512 => "sha512",
        }
    }
}

#[derive(Copy, Clone, PartialEq, PartialOrd, Eq, Ord)]
#[repr(usize)]
enum Base {
    Base64,
    Base32,
    Base16,
    SRI,
}

pub const MD5_HASH_SIZE: usize = 16;
pub const SHA1_HASH_SIZE: usize = 20;
pub const SHA256_HASH_SIZE: usize = 32;
pub const SHA512_HASH_SIZE: usize = 64;

#[repr(C, u8)]
pub enum Hash {
    Unknown([u8; 64]),
    Md5([u8; MD5_HASH_SIZE]),
    Sha1([u8; SHA1_HASH_SIZE]),
    Sha256([u8; SHA256_HASH_SIZE]),
    Sha512([u8; SHA512_HASH_SIZE]),
}

impl PartialEq for Hash {
    fn eq(&self, other: &Self) -> bool {
        use Hash::*;
        match (self, other) {
            (Unknown(ref x), Unknown(ref y)) => &x[..] == &y[..],
            (Md5(ref x), Md5(ref y)) => x == y,
            (Sha1(ref x), Sha1(ref y)) => x == y,
            (Sha256(ref x), Sha256(ref y)) => x == y,
            (Sha512(ref x), Sha512(ref y)) => &x[..] == &y[..],
            _ => false,
        }
    }
}

impl Eq for Hash {}

impl PartialOrd for Hash {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Hash {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        match (self, other) {
            (Unknown(ref x), Unknown(ref y)) => (&x[..]).cmp(&y[..]),
            (Md5(ref x), Md5(ref y)) => (&x[..]).cmp(&y[..]),
            (Sha1(ref x), Sha1(ref y)) => (&x[..]).cmp(&y[..]),
            (Sha256(ref x), Sha256(ref y)) => (&x[..]).cmp(&y[..]),
            (Sha512(ref x), Sha512(ref y)) => (&x[..]).cmp(&y[..]),
            (ref x, ref y) => x.algo().cmp(&y.algo()),
        }
    }
}

impl Hash {
    pub fn zero(ht: HashType) -> Self {
        match ht {
            UNKNOWN => Unknown([0; 64]),
            MD5 => Md5([0; MD5_HASH_SIZE]),
            SHA1 => Sha1([0; SHA1_HASH_SIZE]),
            SHA256 => Sha256([0; SHA256_HASH_SIZE]),
            SHA512 => Sha512([0; SHA512_HASH_SIZE]),
        }
    }

    pub fn algo(&self) -> HashType {
        match *self {
            Unknown(_) => UNKNOWN,
            Md5(_) => MD5,
            Sha1(_) => SHA1,
            Sha256(_) => SHA256,
            Sha512(_) => SHA512,
        }
    }

    pub fn hash(&self) -> &[u8] {
        match *self {
            Unknown(ref h) => &h[..],
            Md5(ref h) => &h[..],
            Sha1(ref h) => &h[..],
            Sha256(ref h) => &h[..],
            Sha512(ref h) => &h[..],
        }
    }

    pub fn len(&self) -> usize {
        match *self {
            Unknown(_) => 64,
            Md5(_) => MD5_HASH_SIZE,
            Sha1(_) => SHA1_HASH_SIZE,
            Sha256(_) => SHA256_HASH_SIZE,
            Sha512(_) => SHA512_HASH_SIZE,
        }
    }

    pub fn print_hash16_or_32(&self) -> String {
        self.to_string(if self.algo() == MD5 { Base16 } else { Base32 }, false)
    }

    pub fn to_string(&self, base: Base, include_algo: bool) -> String {
        let mut s = String::new();
        if base == SRI || include_algo {
            s += self.algo().print_hash_type();
            s += if base == Base::SRI { "-" } else { ":" };
        }
        match base {
            Base16 => s += &base16::encode(self.hash()),
            Base32 => s += &base32::encode(self.hash()),
            Base64 | SRI => s += &base64::encode(self.hash()),
        }
        s
    }
}
