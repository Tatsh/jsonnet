/*
Copyright 2015 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef JSONNET_CMD_JSONNET_DOC_SCAN_H
#define JSONNET_CMD_JSONNET_DOC_SCAN_H

#include <string>
#include <vector>

namespace jsonnet_doc {

/** Normalize a Doxygen comment block (strip leading * and whitespace from lines). */
std::string normalize_doc_block(std::string block);

/** True if block looks like a file-level @file block. */
bool is_file_block(const std::string &block);

/** Sanitize a key for use as identifier (C++ or ReST). */
std::string sanitize_id(const std::string &key);

/** Documented key in the hierarchy. */
struct DocKey {
  std::vector<std::string> path;
  std::string key;
  std::string type;  // number, string, boolean, object, array, mixed
  std::string doc;
};

/** File-level or scope-level doc block. */
struct DocBlock {
  std::string content;
  bool is_file;
  size_t path_depth;
  std::vector<std::string> path;
};

/** Scanner state for one Jsonnet source. */
struct ScanState {
  const std::string &src;
  size_t i = 0;
  std::string pending_doc{};
  bool pending_doc_is_file = false;
  std::vector<std::string> path{};
  std::vector<DocBlock> file_blocks{};
  std::vector<DocKey> keys{};
  unsigned brace_depth = 0;
  bool in_object = false;
};

/** Scan content and populate file_blocks and keys. */
void scan_file(ScanState &s);

/** True if key or any path segment has a leading underscore (private). */
bool path_or_key_has_underscore_prefix(const std::vector<std::string> &path,
                                       const std::string &key);

/** True when this key should be skipped by default (no doc, or _ prefix). */
bool should_skip_key(const DocKey &dk, bool include_private);

/** Build scope path string (e.g. "root::server::port" for C++). */
std::string scope_path(const std::vector<std::string> &path,
                       const std::string &key);

}  // namespace jsonnet_doc

#endif  // JSONNET_CMD_JSONNET_DOC_SCAN_H
