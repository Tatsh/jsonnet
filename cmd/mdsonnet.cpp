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
 * mdsonnet - Convert Jsonnet files to Markdown documentation.
 *
 * Reads Jsonnet files from argv[1] onward, extracts file-level and
 * Doxygen-style block comments plus the structural hierarchy, and writes
 * Markdown (headings and paragraphs) to stdout.
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "jsonnet_doc_scan.h"

namespace {

const char *md_type(const std::string &type) {
  if (type == "number") return "number";
  if (type == "string") return "string";
  if (type == "boolean") return "boolean";
  if (type == "object") return "object";
  if (type == "array") return "array";
  return "mixed";
}

void emit_markdown(const std::string &filename, const std::string &content,
                   std::ostream &out, bool include_private) {
  jsonnet_doc::ScanState s{content};
  jsonnet_doc::scan_file(s);

  std::string file_title = filename;
  std::string file_intro;
  for (const auto &b : s.file_blocks) {
    if (b.is_file) {
      std::string n = jsonnet_doc::normalize_doc_block(b.content);
      size_t at = n.find("@file");
      if (at != std::string::npos) {
        size_t start = n.find_first_not_of(" \t\n", at + 5);
        if (start != std::string::npos) {
          size_t end = n.find('\n', start);
          if (end != std::string::npos) {
            file_title = n.substr(start, end - start);
            size_t trim = file_title.find_last_not_of(" \t");
            if (trim != std::string::npos)
              file_title = file_title.substr(0, trim + 1);
          }
        }
      }
      file_intro = b.content;
      break;
    }
  }

  out << "# " << file_title << "\n\n";
  if (!file_intro.empty()) {
    std::istringstream is(file_intro);
    std::string line;
    while (std::getline(is, line))
      out << line << "\n";
    out << "\n";
  }

  bool any_visible = false;
  for (const auto &dk : s.keys) {
    if (!jsonnet_doc::should_skip_key(dk, include_private)) {
      any_visible = true;
      break;
    }
  }
  if (!any_visible)
    return;

  std::vector<std::vector<std::string>> seen_sections;
  for (const auto &dk : s.keys) {
    if (jsonnet_doc::should_skip_key(dk, include_private))
      continue;
    for (size_t depth = 1; depth <= dk.path.size(); depth++) {
      std::vector<std::string> p(dk.path.begin(), dk.path.begin() + depth);
      bool seen = false;
      for (const auto &sp : seen_sections) {
        if (sp == p) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        seen_sections.push_back(p);
        std::string title = jsonnet_doc::sanitize_id(dk.path[depth - 1]);
        int level = static_cast<int>(depth) + 1;
        if (level > 6) level = 6;
        out << std::string(level, '#') << " " << title << "\n\n";
      }
    }

    std::string title = jsonnet_doc::sanitize_id(dk.key);
    int level = static_cast<int>(dk.path.size()) + 2;
    if (level > 6) level = 6;
    if (dk.type == "object") {
      std::vector<std::string> obj_path = dk.path;
      obj_path.push_back(dk.key);
      seen_sections.push_back(obj_path);
      out << std::string(level, '#') << " " << title << "\n\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          out << line << "\n";
        out << "\n";
      }
    } else {
      out << std::string(level, '#') << " " << title << "\n\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          out << line << "\n";
        out << "\n";
      }
      out << "*Type:* " << md_type(dk.type) << "\n\n";
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
      std::cerr << "mdsonnet: unknown option " << arg << "\n";
      return 1;
    }
    files.push_back(arg);
  }
  if (files.empty()) {
    std::cerr << "Usage: mdsonnet [--include-private] <file.jsonnet> [file2.jsonnet ...]\n";
    std::cerr << "Reads Jsonnet files and writes Markdown to stdout.\n";
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
      std::cerr << "mdsonnet: cannot open " << path << "\n";
      return 1;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();
    emit_markdown(path, content, std::cout, include_private);
    if (i + 1 < files.size())
      std::cout << "\n";
  }
  return 0;
}
