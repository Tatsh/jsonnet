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

/**
 * doxysonnet - Convert Jsonnet files to C++-style Doxygen documentation.
 *
 * Reads Jsonnet files from argv[1] onward, extracts file-level (at-file),
 * block comment (slash-star-star and slash-star-bang) doc blocks and the
 * structural hierarchy (object keys and types), and writes Doxygen-friendly
 * C++-style docblocks to stdout. Intended for use with Doxygen FILTER_PATTERNS.
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "jsonnet_doc_scan.h"

namespace {

const char *cpp_type(const std::string &type) {
  if (type == "number") return "double";
  if (type == "string") return "std::string";
  if (type == "boolean") return "bool";
  if (type == "object") return "mixed";
  if (type == "array") return "mixed";
  return "mixed";
}

void emit_doxygen(const std::string &filename, const std::string &content,
                  std::ostream &out, bool include_private) {
  jsonnet_doc::ScanState s{content};
  jsonnet_doc::scan_file(s);

  bool has_file_block = false;
  for (const auto &b : s.file_blocks) {
    if (b.is_file) {
      out << "/**\n";
      std::istringstream is(b.content);
      std::string line;
      while (std::getline(is, line))
        out << " * " << line << "\n";
      out << " */\n\n";
      has_file_block = true;
      break;
    }
  }
  if (!has_file_block) {
    out << "/** @file " << filename << "\n";
    out << " *  Jsonnet source file.\n";
    out << " */\n\n";
  }

  bool any_visible = false;
  for (const auto &dk : s.keys) {
    if (!jsonnet_doc::should_skip_key(dk, include_private)) {
      any_visible = true;
      break;
    }
  }
  if (any_visible) {
    out << "/** @namespace root\n";
    out << " *  Root object of the Jsonnet file.\n";
    out << " */\n\n";
  }

  std::vector<std::vector<std::string>> seen_namespaces;
  for (const auto &dk : s.keys) {
    if (jsonnet_doc::should_skip_key(dk, include_private))
      continue;
    for (size_t depth = 1; depth <= dk.path.size(); depth++) {
      std::vector<std::string> p(dk.path.begin(), dk.path.begin() + depth);
      bool seen = false;
      for (const auto &sp : seen_namespaces) {
        if (sp == p) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        seen_namespaces.push_back(p);
        std::string ns = jsonnet_doc::scope_path(p, "");
        out << "/** @namespace " << ns << "\n";
        out << " *  Nested object scope.\n";
        out << " */\n\n";
      }
    }

    std::string full_scope = jsonnet_doc::scope_path(dk.path, dk.key);
    if (dk.type == "object") {
      out << "/** @struct " << full_scope << "\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          out << " * " << line << "\n";
      } else {
        out << " *  Nested object.\n";
      }
      out << " */\n\n";
    } else {
      out << "/** @var " << cpp_type(dk.type) << " " << full_scope << "\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          out << " * " << line << "\n";
      }
      out << " */\n\n";
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  bool include_private = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--include-private") {
      include_private = true;
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "doxysonnet: unknown option " << arg << "\n";
      return 1;
    }
    files.push_back(arg);
  }
  if (files.empty()) {
    std::cerr << "Usage: doxysonnet [--include-private] <file.jsonnet> [file2.jsonnet ...]\n";
    std::cerr << "Reads Jsonnet files and writes C++-style Doxygen docblocks to stdout.\n";
    std::cerr << "By default, keys with a leading _ or with no Doxygen comment are omitted.\n";
    std::cerr << "Pass --include-private to include them.\n";
    return 1;
  }
  for (size_t i = 0; i < files.size(); i++) {
    const std::string &path = files[i];
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
      continue;
    std::ifstream f(path);
    if (!f) {
      std::cerr << "doxysonnet: cannot open " << path << "\n";
      return 1;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();
    emit_doxygen(path, content, std::cout, include_private);
    if (i + 1 < files.size())
      std::cout << "\n";
  }
  return 0;
}
