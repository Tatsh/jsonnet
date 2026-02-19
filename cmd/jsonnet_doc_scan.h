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

#include <ostream>
#include <string>
#include <vector>

namespace jsonnet_doc {

/** Normalize a Doxygen comment block (strip leading * and whitespace from lines). */
std::string normalize_doc_block(std::string block);

/** True if block looks like a file-level @file block. */
bool is_file_block(const std::string &block);

/** If doc contains a line \"@var <type>\", remove that line from doc and return the type
 *  (normalized: \"bool\" -> \"boolean\"); otherwise return empty string.
 */
std::string extract_var_type(std::string &doc);

/** If doc contains a line \"@rv <type>\", remove that line from doc and return the type
 *  (normalized: \"bool\" -> \"boolean\"); otherwise return empty string.
 */
std::string extract_rv_type(std::string &doc);

/** If doc contains a line \"@pt type1, type2, ...\", remove that line from doc and
 *  return the comma-separated types in order; otherwise return empty vector.
 */
std::vector<std::string> extract_pt_types(std::string &doc);

/** If doc contains a line \"@namespace <name>\", remove that line and return the name
 *  (single token, no spaces); otherwise return empty string.
 */
std::string extract_namespace(std::string &doc);

/** If doc contains a line \"@brief ...\" or \"\\brief ...\", return the rest of that
 *  line (trimmed); otherwise return empty string. Does not modify doc.
 */
std::string extract_brief(const std::string &doc);

/** Sanitize a key for use as identifier (C++ or ReST). */
std::string sanitize_id(const std::string &key);

/** Documented key in the hierarchy. */
struct DocKey {
  std::vector<std::string> path;
  std::string key;
  std::string type;  // number, string, boolean, object, array, mixed, function
  std::string doc;
  /** When type == "function", comma-separated parameter names from key(...). */
  std::string function_params;
  /** Verbatim source of the value (unevaluated). Empty for functions; optional in scanner for nested objects. */
  std::string value_verbatim;
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

/** Scan content and populate file_blocks and keys (lightweight scanner). */
void scan_file(ScanState &s);

/** Parse content with the real Jsonnet parser and populate file_blocks and keys.
 *  On parse error, leaves file_blocks and keys unchanged (no throw).
 */
void parse_file_to_doc_state(const std::string &filename, const std::string &content,
                              ScanState &s);

/** True if key or any path segment has a leading underscore (private). */
bool path_or_key_has_underscore_prefix(const std::vector<std::string> &path,
                                       const std::string &key);

/** True when this key should be skipped by default (no doc, or _ prefix). */
bool should_skip_key(const DocKey &dk, bool include_private);

/** Build scope path string (e.g. "root::server::port" for C++). */
std::string scope_path(const std::vector<std::string> &path,
                       const std::string &key);

/** Word-wrap text to at most width chars per line; write to out.
 * first_indent and cont_indent are space counts for first and continuation lines.
 */
void wrap_lines(std::ostream &out, const std::string &text, int width,
                int first_indent, int cont_indent);

}  // namespace jsonnet_doc

#endif  // JSONNET_CMD_JSONNET_DOC_SCAN_H
