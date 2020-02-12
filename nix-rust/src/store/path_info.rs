use crate::store::StorePath;
use crate::Error;
use std::collections::BTreeSet;

#[derive(Clone, Debug)]
pub struct PathInfo {
    pub path: StorePath,
    pub references: BTreeSet<StorePath>,
    pub nar_size: u64,
    pub deriver: Option<StorePath>,

    // Additional binary cache info.
    pub url: Option<String>,
    pub compression: Option<String>,
    pub file_size: Option<u64>,
}

impl PathInfo {
    pub fn parse_nar_info(nar_info: &str, store_dir: &str) -> Result<Self, Error> {
        let mut path = None;
        let mut references = BTreeSet::new();
        let mut nar_size = None;
        let mut deriver = None;
        let mut url = None;
        let mut compression = None;
        let mut file_size = None;

        for line in nar_info.lines() {
            let colon = line.find(':').ok_or(Error::BadNarInfo)?;

            let (name, value) = line.split_at(colon);

            if !value.starts_with(": ") {
                return Err(Error::BadNarInfo);
            }

            let value = &value[2..];

            if name == "StorePath" {
                path = Some(StorePath::new(std::path::Path::new(value), store_dir)?);
            } else if name == "NarSize" {
                nar_size = Some(u64::from_str_radix(value, 10).map_err(|_| Error::BadNarInfo)?);
            } else if name == "References" {
                if !value.is_empty() {
                    for r in value.split(' ') {
                        references.insert(StorePath::new_from_base_name(r)?);
                    }
                }
            } else if name == "Deriver" {
                deriver = Some(StorePath::new_from_base_name(value)?);
            } else if name == "URL" {
                url = Some(value.into());
            } else if name == "Compression" {
                compression = Some(value.into());
            } else if name == "FileSize" {
                file_size = Some(u64::from_str_radix(value, 10).map_err(|_| Error::BadNarInfo)?);
            }
        }

        Ok(PathInfo {
            path: path.ok_or(Error::BadNarInfo)?,
            references,
            nar_size: nar_size.ok_or(Error::BadNarInfo)?,
            deriver,
            url: Some(url.ok_or(Error::BadNarInfo)?),
            compression,
            file_size,
        })
    }
}
