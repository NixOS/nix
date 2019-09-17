use super::{PathInfo, StorePath};
use crate::Error;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

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
