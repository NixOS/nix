/*
 * JSON schema validator for JSON for modern C++
 *
 * Copyright (c) 2016-2019 Patrick Boettcher <p@yai.se>.
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include <nlohmann/json-schema.hpp>

#include <sstream>

namespace nlohmann
{

void json_uri::update(const std::string &uri)
{
	std::string pointer = ""; // default pointer is document-root

	// first split the URI into location and pointer
	auto pointer_separator = uri.find('#');
	if (pointer_separator != std::string::npos) {    // and extract the pointer-string if found
		pointer = uri.substr(pointer_separator + 1); // remove #

		// unescape %-values IOW, decode JSON-URI-formatted JSON-pointer
		std::size_t pos = pointer.size() - 1;
		do {
			pos = pointer.rfind('%', pos);
			if (pos == std::string::npos)
				break;

			if (pos >= pointer.size() - 2) {
				pos--;
				continue;
			}

			std::string hex = pointer.substr(pos + 1, 2);
			char ascii = static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16));
			pointer.replace(pos, 3, 1, ascii);

			pos--;
		} while (1);
	}

	auto location = uri.substr(0, pointer_separator);

	if (location.size()) { // a location part has been found

		// if it is an URN take it as it is
		if (location.find("urn:") == 0) {
			urn_ = location;

			// and clear URL members
			scheme_ = "";
			authority_ = "";
			path_ = "";

		} else { // it is an URL

			// split URL in protocol, hostname and path
			std::size_t pos = 0;
			auto proto = location.find("://", pos);
			if (proto != std::string::npos) { // extract the protocol

				urn_ = ""; // clear URN-member if URL is parsed

				scheme_ = location.substr(pos, proto - pos);
				pos = 3 + proto; // 3 == "://"

				auto authority = location.find("/", pos);
				if (authority != std::string::npos) { // and the hostname (no proto without hostname)
					authority_ = location.substr(pos, authority - pos);
					pos = authority;
				}
			}

			auto path = location.substr(pos);

			// URNs cannot of have paths
			if (urn_.size() && path.size())
				throw std::invalid_argument("Cannot add a path (" + path + ") to an URN URI (" + urn_ + ")");

			if (path[0] == '/') // if it starts with a / it is root-path
				path_ = path;
			else if (pos == 0) { // the URL contained only a path and the current path has no / at the end, strip last element until / and append
				auto last_slash = path_.rfind('/');
				path_ = path_.substr(0, last_slash) + '/' + path;
			} else // otherwise it is a subfolder
				path_.append(path);
		}
	}

	pointer_ = ""_json_pointer;
	identifier_ = "";

	if (pointer[0] == '/')
		pointer_ = json::json_pointer(pointer);
	else
		identifier_ = pointer;
}

std::string json_uri::location() const
{
	if (urn_.size())
		return urn_;

	std::stringstream s;

	if (scheme_.size() > 0)
		s << scheme_ << "://";

	s << authority_
	  << path_;

	return s.str();
}

std::string json_uri::to_string() const
{
	std::stringstream s;

	s << location() << " # ";

	if (identifier_ == "")
		s << pointer_.to_string();
	else
		s << identifier_;

	return s.str();
}

std::ostream &operator<<(std::ostream &os, const json_uri &u)
{
	return os << u.to_string();
}

std::string json_uri::escape(const std::string &src)
{
	std::vector<std::pair<std::string, std::string>> chars = {
	    {"~", "~0"},
	    {"/", "~1"}};

	std::string l = src;

	for (const auto &c : chars) {
		std::size_t pos = 0;
		do {
			pos = l.find(c.first, pos);
			if (pos == std::string::npos)
				break;
			l.replace(pos, 1, c.second);
			pos += c.second.size();
		} while (1);
	}

	return l;
}

} // namespace nlohmann
