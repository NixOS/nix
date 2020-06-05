use crate::Error;
use byteorder::{LittleEndian, ReadBytesExt};
use std::convert::TryFrom;
use std::io::Read;

pub fn parse<R: Read>(input: &mut R) -> Result<(), Error> {
    if String::read(input)? != NAR_VERSION_MAGIC {
        return Err(Error::BadNarVersionMagic);
    }

    parse_file(input)
}

const NAR_VERSION_MAGIC: &str = "nix-archive-1";

fn parse_file<R: Read>(input: &mut R) -> Result<(), Error> {
    if String::read(input)? != "(" {
        return Err(Error::MissingNarOpenTag);
    }

    if String::read(input)? != "type" {
        return Err(Error::MissingNarField);
    }

    match String::read(input)?.as_ref() {
        "regular" => {
            let mut _executable = false;
            let mut tag = String::read(input)?;
            if tag == "executable" {
                _executable = true;
                if String::read(input)? != "" {
                    return Err(Error::BadExecutableField);
                }
                tag = String::read(input)?;
            }
            if tag != "contents" {
                return Err(Error::MissingNarField);
            }
            let _contents = Vec::<u8>::read(input)?;
            if String::read(input)? != ")" {
                return Err(Error::MissingNarCloseTag);
            }
        }
        "directory" => loop {
            match String::read(input)?.as_ref() {
                "entry" => {
                    if String::read(input)? != "(" {
                        return Err(Error::MissingNarOpenTag);
                    }
                    if String::read(input)? != "name" {
                        return Err(Error::MissingNarField);
                    }
                    let _name = String::read(input)?;
                    if String::read(input)? != "node" {
                        return Err(Error::MissingNarField);
                    }
                    parse_file(input)?;
                    let tag = String::read(input)?;
                    if tag != ")" {
                        return Err(Error::MissingNarCloseTag);
                    }
                }
                ")" => break,
                s => return Err(Error::BadNarField(s.into())),
            }
        },
        "symlink" => {
            if String::read(input)? != "target" {
                return Err(Error::MissingNarField);
            }
            let _target = String::read(input)?;
            if String::read(input)? != ")" {
                return Err(Error::MissingNarCloseTag);
            }
        }
        s => return Err(Error::BadNarField(s.into())),
    }

    Ok(())
}

trait Deserialize: Sized {
    fn read<R: Read>(input: &mut R) -> Result<Self, Error>;
}

impl Deserialize for String {
    fn read<R: Read>(input: &mut R) -> Result<Self, Error> {
        let buf = Deserialize::read(input)?;
        Ok(String::from_utf8(buf).map_err(|_| Error::BadNarString)?)
    }
}

impl Deserialize for Vec<u8> {
    fn read<R: Read>(input: &mut R) -> Result<Self, Error> {
        let n: usize = Deserialize::read(input)?;
        let mut buf = vec![0; n];
        input.read_exact(&mut buf)?;
        skip_padding(input, n)?;
        Ok(buf)
    }
}

fn skip_padding<R: Read>(input: &mut R, len: usize) -> Result<(), Error> {
    if len % 8 != 0 {
        let mut buf = [0; 8];
        let buf = &mut buf[0..8 - (len % 8)];
        input.read_exact(buf)?;
        if !buf.iter().all(|b| *b == 0) {
            return Err(Error::BadNarPadding);
        }
    }
    Ok(())
}

impl Deserialize for u64 {
    fn read<R: Read>(input: &mut R) -> Result<Self, Error> {
        Ok(input.read_u64::<LittleEndian>()?)
    }
}

impl Deserialize for usize {
    fn read<R: Read>(input: &mut R) -> Result<Self, Error> {
        let n: u64 = Deserialize::read(input)?;
        Ok(usize::try_from(n).map_err(|_| Error::NarSizeFieldTooBig)?)
    }
}
