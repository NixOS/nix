#include "ipfs-accessor.hh"
#include "download.hh"
#include "globals.hh"
#include <iostream>
#include "ipfs/client.h"

using namespace nix;

IPFSAccessor::IPFSAccessor() {
}

void IPFSAccessor::getFile(const std::string & hash,
                           std::function<void(std::shared_ptr<std::string>)> success,
                           std::function<void(std::exception_ptr exc)> failure) {
  std::string uri;
  if (settings.useIpfsGateway == false) {
    /*  use API */
    uri = ipfs::buildAPIURL(settings.ipfsAPIHost, settings.ipfsAPIPort) +
          ipfs::buildCommandURL("cat_gw", hash);
  } else {
    uri = settings.ipfsGatewayURL + "/ipfs" +
          ipfs::buildCommandURL("cat", hash);
  }
  DownloadRequest request(uri);
  request.showProgress = DownloadRequest::no;
  request.tries = 8;
  getDownloader()->enqueueDownload(request,
      [success](const DownloadResult & result) {
        success(result.data);
      },
      [success, failure](std::exception_ptr exc) {
        failure(exc);
      });
}

std::shared_ptr<std::string> IPFSAccessor::getFile(const std::string & hash) {
  std::promise<std::shared_ptr<std::string>> promise;
  getFile(hash,
      [&](const std::shared_ptr<std::string> & result) {
          promise.set_value(result);
      },
      [&](std::exception_ptr exc) {
          promise.set_exception(exc);
      });
  return promise.get_future().get();
}

std::string IPFSAccessor::addFile(const std::string & filename,
                    const std::string & content) {
    ipfs::Client client(settings.ipfsAPIHost, settings.ipfsAPIPort);
    ipfs::Json result;
    client.FilesAdd({{filename, ipfs::http::FileUpload::Type::kFileContents, content}}, &result);
    auto ipfsHashIt = result.at(0).find("hash");
    if (ipfsHashIt != result.at(0).end())
      return *ipfsHashIt;
    else
      return "";
}
