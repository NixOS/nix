use crate::store::{Store, StorePath};
use crate::path_info::PathInfo;
use crate::Error;
use futures::compat::Future01CompatExt;

pub struct BinaryCacheStore {
    base_uri: String,
    client: reqwest::r#async::Client,
}

impl BinaryCacheStore {
    pub fn new(base_uri: String) -> Self {
        Self {
            base_uri,
            client: reqwest::r#async::Client::new(),
        }
    }
}

impl Store for BinaryCacheStore {
    fn query_path_info(
        &self,
        path: &StorePath,
    ) -> std::pin::Pin<Box<dyn std::future::Future<Output = Result<PathInfo, Error>> + Send>> {
        let uri = format!("{}/{}.narinfo", self.base_uri.clone(), path.hash);
        let path = path.clone();
        let client = self.client.clone();
        let store_dir = self.store_dir().to_string();

        Box::pin(async move {
            let response = client
                .get(&uri)
                .send()
                .compat()
                .await?;

            if response.status() == reqwest::StatusCode::NOT_FOUND || response.status() == reqwest::StatusCode::FORBIDDEN {
                return Err(Error::InvalidPath(path));
            }

            let mut response = response.error_for_status()?;

            let body = response
                .text()
                .compat()
                .await?;

            PathInfo::parse_nar_info(&body, &store_dir)
        })
    }
}
