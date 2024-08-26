#include <array>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace lfs {

// git-lfs metadata about a file
struct Md {
  std::string path; // fs path relative to repo root, no ./ prefix
  std::string oid; // git-lfs managed object id. you give this to the lfs server
                   // for downloads
  size_t size;     // in bytes
};

std::string exec_command(const std::string &cmd) {
  std::cout << cmd << std::endl;
  std::string data;
  std::array<char, 256> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  if (result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

std::string get_lfs_api_token(const std::string &host,
                              const std::string &path) {
  std::string command =
      "ssh git@" + host + " git-lfs-authenticate " + path + " download";
  std::string output = exec_command(command);
  std::string res;

  if (!output.empty()) {
    nlohmann::json query_resp = nlohmann::json::parse(output);
    res = query_resp["header"]["Authorization"].get<std::string>();
  }

  return res;
}

std::tuple<std::string, std::string, std::string>
get_lfs_endpoint_url(git_repository *repo) {
  int err;
  git_config *config = nullptr;
  // using git_repository_config instead of _snapshot makes the
  // git_config_get_string call fail why????
  if (git_repository_config_snapshot(&config, repo) < 0) {
    // Handle error if necessary
    git_config_free(config);
    std::cerr << "no config" << std::endl;
    return {"", "", ""};
  }

  const char *url_c_str;
  err = git_config_get_string(&url_c_str, config, "remote.origin.url");
  if (err < 0) {
    // Handle error if necessary
    std::cerr << "no remote.origin.url: " << err << std::endl;
    git_config_free(config);
    return {"", "", ""};
  }
  std::string url = std::string(url_c_str);
  std::cerr << "url_c_str: " << url_c_str << std::endl;

  if (url.back() == '/') {
    url.pop_back();
  }

  // idk what this was for man
  // if (url.compare(url.length() - 4, 4, ".git") != 0) {
  // url += "/info/lfs";
  //} else {
  //    url += ".git/info/lfs";
  //}

  // Parse the URL
  std::string scheme, host, path;
  if (url.find("https://") != 0) {
    size_t at_pos = url.find('@');
    if (at_pos != std::string::npos) {
      host = url.substr(at_pos + 1);
      size_t colon_pos = host.find(':');
      path = host.substr(colon_pos + 1);
      host = host.substr(0, colon_pos);
      scheme = "https";
      url = scheme + "://" + host + "/" + path;
    }
  }

  return std::make_tuple(url, host, path);
}

std::string git_attr_value_to_string(git_attr_value_t value) {
  switch (value) {
  case GIT_ATTR_VALUE_UNSPECIFIED:
    return "GIT_ATTR_VALUE_UNSPECIFIED";
  case GIT_ATTR_VALUE_TRUE:
    return "GIT_ATTR_VALUE_TRUE";
  case GIT_ATTR_VALUE_FALSE:
    return "GIT_ATTR_VALUE_FALSE";
  case GIT_ATTR_VALUE_STRING:
    return "GIT_ATTR_VALUE_STRING";
  default:
    return "Unknown value";
  }
}

std::unique_ptr<std::vector<std::string>> find_lfs_files(git_repository *repo) {
  git_index *index;
  int error;
  error = git_repository_index(&index, repo);
  if (error < 0) {
    const git_error *e = git_error_last();
    std::cerr << "Error reading index: " << e->message << std::endl;
    git_repository_free(repo);
    return nullptr;
  }

  std::unique_ptr<std::vector<std::string>> out =
      std::make_unique<std::vector<std::string>>();
  size_t entry_count = git_index_entrycount(index);
  for (size_t i = 0; i < entry_count; ++i) {
    const git_index_entry *entry = git_index_get_byindex(index, i);
    if (entry) {
      const char *value;
      if (git_attr_get(&value, repo, GIT_ATTR_CHECK_INDEX_ONLY, entry->path,
                       "filter") == 0) {
        auto value_type = git_attr_value(value);
        if (value_type == GIT_ATTR_VALUE_STRING && strcmp(value, "lfs") == 0) {
          out->push_back(entry->path);
        }
      }
    }
  }
  return out;
}

std::string get_obj_content(git_repository *repo, std::string path) {
  // Get the HEAD commit
  git_object *obj = nullptr;
  if (git_revparse_single(&obj, repo, "HEAD^{commit}") != 0) {
    std::cerr << "Failed to find HEAD" << std::endl;
    return "";
  }

  git_commit *commit = nullptr;
  git_commit_lookup(&commit, repo, git_object_id(obj));
  git_object_free(obj);

  git_tree *tree = nullptr;
  if (git_commit_tree(&tree, commit) != 0) {
    std::cerr << "Failed to get tree from commit" << std::endl;
    git_commit_free(commit);
    return "";
  }

  git_tree_entry *entry;
  if (git_tree_entry_bypath(&entry, tree, path.c_str()) != 0) {
    std::cerr << "Failed to find " << path << " in the tree" << std::endl;
    git_tree_free(tree);
    git_commit_free(commit);
    return "";
  }

  git_blob *blob = nullptr;
  if (git_blob_lookup(&blob, repo, git_tree_entry_id(entry)) != 0) {
    std::cerr << "Failed to lookup blob" << std::endl;
    git_tree_entry_free(
        const_cast<git_tree_entry *>(entry)); // Free entry after use
    git_tree_free(tree);
    git_commit_free(commit);
    return "";
  }

  auto content = git_blob_rawcontent(blob);
  git_object_free(obj);
  git_tree_free(tree);
  git_commit_free(commit);

  // is this copy elided? i wont pretend to understand
  // https://en.cppreference.com/w/cpp/language/copy_elision
  return std::string(static_cast<const char *>(content));
}

std::tuple<std::string, size_t> parse_lfs_metadata(const std::string &content) {
  std::istringstream iss(content);
  std::string line;
  std::string oid;
  size_t size = 0;

  while (getline(iss, line)) {
    std::size_t pos = line.find("oid sha256:");
    if (pos != std::string::npos) {
      oid = line.substr(pos + 11); // Extract hash after "oid sha256:"
      continue;
    }

    pos = line.find("size ");
    if (pos != std::string::npos) {
      std::string sizeStr =
          line.substr(pos + 5); // Extract size number after "size "
      size = std::stol(sizeStr);
      continue;
    }
  }

  return std::make_tuple(oid, size);
}

// path, oid, size
std::vector<Md> parse_lfs_files(git_repository *repo) {
  const auto files = find_lfs_files(repo);
  std::vector<Md> out;
  for (const auto &file : *files) {
    std::cerr << file;
    auto content = get_obj_content(repo, file);
    auto [oid, size] = parse_lfs_metadata(content);
    out.push_back(Md{file, oid, size});
  }

  return out;
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *s) {
  size_t newLength = size * nmemb;
  try {
    s->append((char *)contents, newLength);
  } catch (std::bad_alloc &e) {
    // Handle memory bad_alloc error
    return 0;
  }
  return newLength;
}

nlohmann::json oids_to_payload(const std::vector<Md> &items) {
  nlohmann::json j_array = nlohmann::json::array();
  for (const auto &md : items) {
    j_array.push_back({{"oid", md.oid}, {"size", md.size}});
  }
  return j_array;
}

std::vector<nlohmann::json> fetch_urls(const std::string &lfs_url,
                                       const std::string &token,
                                       const std::vector<Md> &metadatas) {
  std::vector<nlohmann::json> objects;

  nlohmann::json oid_list = oids_to_payload(metadatas);
  nlohmann::json data = {
      {"operation", "download"},
  };
  data["objects"] = oid_list;
  auto data_str = data.dump();
  std::cerr << "data_str: " + data_str << std::endl;

  CURL *curl = curl_easy_init();
  std::string response_string;
  std::string header_string;
  auto lfs_url_batch = lfs_url + "/info/lfs/objects/batch";
  curl_easy_setopt(curl, CURLOPT_URL, lfs_url_batch.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data_str.c_str());

  // Set headers
  struct curl_slist *headers = NULL;
  auto auth_header = "Authorization: " + token;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers =
      curl_slist_append(headers, "Content-Type: application/vnd.git-lfs+json");
  headers = curl_slist_append(headers, "Accept: application/vnd.git-lfs+json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

  CURLcode res = curl_easy_perform(curl);
  // Check for errors
  if (res != CURLE_OK)
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  try {
    std::cerr << "resp: " << response_string << std::endl;
    // example resp here:
    // {"objects":[{"oid":"f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","size":10000000,"actions":{"download":{"href":"https://gitlab.com/b-camacho/test-lfs.git/gitlab-lfs/objects/f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","header":{"Authorization":"Basic
    // Yi1jYW1hY2hvOmV5SjBlWEFpT2lKS1YxUWlMQ0poYkdjaU9pSklVekkxTmlKOS5leUprWVhSaElqcDdJbUZqZEc5eUlqb2lZaTFqWVcxaFkyaHZJbjBzSW1wMGFTSTZJbUptTURZNFpXVTFMVEprWmpVdE5HWm1ZUzFpWWpRMExUSXpNVEV3WVRReU1qWmtaaUlzSW1saGRDSTZNVGN4TkRZeE16ZzBOU3dpYm1KbUlqb3hOekUwTmpFek9EUXdMQ0psZUhBaU9qRTNNVFEyTWpFd05EVjkuZk9yMDNkYjBWSTFXQzFZaTBKRmJUNnJTTHJPZlBwVW9lYllkT0NQZlJ4QQ=="}}},"authenticated":true}]}
    auto resp = nlohmann::json::parse(response_string);
    if (resp.contains("objects")) {
      objects.insert(objects.end(), resp["objects"].begin(),
                     resp["objects"].end());
    } else {
      throw std::runtime_error("Response does not contain 'objects'");
    }
  } catch (std::exception &e) {
    std::cerr << "Failed to parse JSON or invalid response: " << e.what()
              << std::endl;
  }

  return objects;
}

static size_t WriteData(void *ptr, size_t size, size_t nmemb, void *stream) {
  size_t written = fwrite(ptr, size, nmemb, (FILE *)stream);
  return written;
}

void download_file(const std::string &url, const std::string &auth_header,
                   const std::string &output_filename) {
  CURL *curl;
  FILE *fp;
  CURLcode res;

  curl = curl_easy_init();
  if (curl) {
    fp = fopen(output_filename.c_str(), "wb");
    if (fp == nullptr) {
      std::cerr << "Failed to open file for writing: " << output_filename
                << std::endl;
      return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist *headers = nullptr;
    const std::string auth_header_prepend = "Authorization: " + auth_header;
    headers = curl_slist_append(headers, auth_header_prepend.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(fp);
  }
}

void download_files(nlohmann::json objects, std::string dir) {
  for (auto &obj : objects) {
    std::string oid = obj["oid"];
    std::string url = obj["actions"]["download"]["href"];
    std::string auth_header =
        obj["actions"]["download"]["header"]["Authorization"];
    download_file(url, auth_header, dir + "/" + oid);
  }
}

// moves files from temporary download dir to final location
void move_files(const std::vector<Md> &metadata,
                const std::string &sourceDir, const std::string &repoRoot) {
  namespace fs = std::filesystem;

  for (const auto &md : metadata) {
    fs::path srcFile =
        fs::path(sourceDir) / md.oid; // Construct the source file path
    fs::path destFile =
        fs::path(repoRoot) / md.path; // Construct the destination file path

    // Move the file
    try {
      fs::rename(srcFile, destFile);
    } catch (const fs::filesystem_error &e) {
      std::cerr << "Error moving file " << srcFile << " to " << destFile << ": "
                << e.what() << std::endl;
    }
  }
}

} // namespace lfs
