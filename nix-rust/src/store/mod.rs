pub mod path;
pub use path::{StorePath, StorePathHash, StorePathName};

#[cfg(unused)]
mod binary_cache_store;
#[cfg(unused)]
pub use binary_cache_store::BinaryCacheStore;

mod path_info;
pub use path_info::PathInfo;

use crate::Error;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

pub trait Store: Send + Sync {
    fn store_dir(&self) -> &Path {
        Path::new("/nix/store")
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

    #[cfg(feature = "futures-util")]
    pub async fn compute_path_closure(
        &self,
        roots: BTreeSet<StorePath>,
    ) -> Result<BTreeMap<StorePath, PathInfo>, Error> {
        let mut result = BTreeMap::new();
        let mut pending: Vec<_> = roots
            .iter()
            .map(|root| self.query_path_info(root))
            .collect();
        let mut done = roots;

        while !pending.is_empty() {
            let (info, _, remaining) = futures_util::future::select_all(pending).await;
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
