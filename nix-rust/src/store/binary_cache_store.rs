use super::{PathInfo, Store, StorePath};
use crate::Error;
use hyper::client::Client;

pub struct BinaryCacheStore {
    base_uri: String,
    client: Client<hyper::client::HttpConnector, hyper::Body>,
}

impl BinaryCacheStore {
    pub fn new(base_uri: String) -> Self {
        Self {
            base_uri,
            client: Client::new(),
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
            let response = client.get(uri.parse::<hyper::Uri>().unwrap()).await?;

            if response.status() == hyper::StatusCode::NOT_FOUND
                || response.status() == hyper::StatusCode::FORBIDDEN
            {
                return Err(Error::InvalidPath(path));
            }

            let mut body = response.into_body();

            let mut bytes = Vec::new();
            while let Some(next) = body.next().await {
                bytes.extend(next?);
            }

            PathInfo::parse_nar_info(std::str::from_utf8(&bytes).unwrap(), &store_dir)
        })
    }
}
