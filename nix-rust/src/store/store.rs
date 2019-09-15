use super::PathInfo;
use crate::Error;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct StorePath {
    pub hash: String,
    pub name: String,
}

pub const STORE_PATH_HASH_CHARS: usize = 32;

impl StorePath {
    pub fn new(path: &Path, store_dir: &str) -> Result<Self, Error> {
        // FIXME: check store_dir
        Self::new_short(
            path.file_name()
                .ok_or(Error::BadStorePath(path.into()))?
                .to_str()
                .ok_or(Error::BadStorePath(path.into()))?,
        )
    }

    pub fn new_short(base_name: &str) -> Result<Self, Error> {
        if base_name.len() < STORE_PATH_HASH_CHARS + 2
            || base_name.as_bytes()[STORE_PATH_HASH_CHARS] != '-' as u8
        {
            return Err(Error::BadStorePath(base_name.into()));
        }

        // FIXME: validate name

        Ok(StorePath {
            hash: base_name[0..STORE_PATH_HASH_CHARS].to_string(),
            name: base_name[STORE_PATH_HASH_CHARS + 1..].to_string(),
        })
    }
}

#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct StorePathHash {
    bytes: [u8; 20],
}

/*
impl StorePathHash {
    pub fn to_base32(&self) -> String {
        "7h7qgvs4kgzsn8a6rb273saxyqh4jxlz".to_string()
    }
}
*/

pub trait Store: Send + Sync {
    fn store_dir(&self) -> &str {
        "/nix/store"
    }

    fn query_path_info(
        &self,
        store_path: &StorePath,
    ) -> std::pin::Pin<Box<dyn std::future::Future<Output = Result<PathInfo, Error>> + Send>>;
}

impl dyn Store {
    pub fn parse_store_path(&self, path: &Path) -> Result<StorePath, Error> {
        StorePath::new(path, self.store_dir())
    }

    pub async fn compute_path_closure(
        &self,
        roots: BTreeSet<StorePath>,
    ) -> Result<BTreeMap<StorePath, PathInfo>, Error> {
        let mut done = BTreeSet::new();
        let mut result = BTreeMap::new();
        let mut pending = vec![];

        for root in roots {
            pending.push(self.query_path_info(&root));
            done.insert(root);
        }

        while !pending.is_empty() {
            let (info, _, remaining) = futures::future::select_all(pending).await;
            pending = remaining;

            let info = info?;

            for path in &info.references {
                if !done.contains(path) {
                    pending.push(self.query_path_info(&path));
                    done.insert(path.clone());
                }
            }

            result.insert(info.path.clone(), info);
        }

        Ok(result)
    }
}
