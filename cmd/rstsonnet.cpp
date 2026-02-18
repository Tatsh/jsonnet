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
 * rstsonnet - Convert Jsonnet files to ReStructured Text documentation.
 *
 * Reads Jsonnet files from argv[1] onward, extracts file-level and
 * Doxygen-style block comments plus the structural hierarchy, and writes
 * ReST (sections and definition lists) to stdout.
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "jsonnet_doc_scan.h"

namespace {

static const char REST_UNDERLINES[] = "=-^\"#";

void emit_rest(const std::string &filename, const std::string &content,
               std::ostream &out, bool include_private, int indent, int width) {
  jsonnet_doc::ScanState s{content};
  jsonnet_doc::parse_file_to_doc_state(filename, content, s);

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

  out << file_title << "\n";
  out << std::string(file_title.size(), REST_UNDERLINES[0]) << "\n\n";
  if (!file_intro.empty()) {
    std::istringstream is(file_intro);
    std::string line;
    while (std::getline(is, line))
      jsonnet_doc::wrap_lines(out, line, width, 0, 0);
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
        char ul = REST_UNDERLINES[std::min(depth, sizeof(REST_UNDERLINES) - 1)];
        out << title << "\n";
        out << std::string(title.size(), ul) << "\n\n";
      }
    }

    std::string title = jsonnet_doc::sanitize_id(dk.key);
    char ul_char = REST_UNDERLINES[std::min(dk.path.size() + 1,
                                            sizeof(REST_UNDERLINES) - 1)];
    if (dk.type == "object") {
      std::vector<std::string> obj_path = dk.path;
      obj_path.push_back(dk.key);
      seen_sections.push_back(obj_path);
      out << title << "\n";
      out << std::string(title.size(), ul_char) << "\n\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          jsonnet_doc::wrap_lines(out, line, width, 0, 0);
        out << "\n";
      }
    } else {
      out << title << "\n";
      if (!dk.doc.empty())
        jsonnet_doc::wrap_lines(out, dk.doc, width, indent, indent);
      std::string type_line = "**Type:** ";
      type_line += dk.type;
      jsonnet_doc::wrap_lines(out, type_line, width, indent, indent);
      out << "\n";
    }
  }
}

}  // namespace

static bool parse_indent(const std::string &val, const char *opt_name,
                         int *out) {
  try {
    size_t pos;
    int n = std::stoi(val, &pos);
    if (pos != val.size())
      throw std::invalid_argument("");
    if (n == -1 || n == 0) {
      *out = 0;
      return true;
    }
    if (n < 0) {
      std::cerr << "rstsonnet: invalid " << opt_name << " value \"" << val
                << "\" (only 0 or -1 allowed for no indent)\n";
      return false;
    }
    *out = n;
    return true;
  } catch (...) {
    std::cerr << "rstsonnet: invalid " << opt_name << " value \"" << val
              << "\"\n";
    return false;
  }
}

static bool parse_width(const std::string &val, const char *opt_name,
                        int *out) {
  try {
    size_t pos;
    int n = std::stoi(val, &pos);
    if (pos != val.size())
      throw std::invalid_argument("");
    if (n == -1 || n == 0) {
      *out = 0;
      return true;
    }
    if (n < 0) {
      std::cerr << "rstsonnet: invalid " << opt_name << " value \"" << val
                << "\" (only 0 or -1 allowed for infinite width)\n";
      return false;
    }
    *out = n;
    return true;
  } catch (...) {
    std::cerr << "rstsonnet: invalid " << opt_name << " value \"" << val
              << "\"\n";
    return false;
  }
}

int main(int argc, char **argv) {
  bool include_private = false;
  int indent = 3;
  int width = 100;
  std::vector<std::string> files;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--include-private") {
      include_private = true;
      continue;
    }
    if (arg.compare(0, 9, "--indent=") == 0) {
      if (!parse_indent(arg.substr(9), "--indent", &indent))
        return 1;
      continue;
    }
    if (arg == "--indent" && i + 1 < argc) {
      if (!parse_indent(argv[++i], "--indent", &indent))
        return 1;
      continue;
    }
    if (arg == "-i" && i + 1 < argc) {
      if (!parse_indent(argv[++i], "-i", &indent))
        return 1;
      continue;
    }
    if (arg.compare(0, 2, "-i") == 0 && arg.size() > 2) {
      if (!parse_indent(arg.substr(2), "-i", &indent))
        return 1;
      continue;
    }
    if (arg.compare(0, 8, "--width=") == 0) {
      if (!parse_width(arg.substr(8), "--width", &width))
        return 1;
      continue;
    }
    if (arg == "--width" && i + 1 < argc) {
      if (!parse_width(argv[++i], "--width", &width))
        return 1;
      continue;
    }
    if (arg == "-w" && i + 1 < argc) {
      if (!parse_width(argv[++i], "-w", &width))
        return 1;
      continue;
    }
    if (arg.compare(0, 2, "-w") == 0 && arg.size() > 2) {
      if (!parse_width(arg.substr(2), "-w", &width))
        return 1;
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "rstsonnet: unknown option " << arg << "\n";
      return 1;
    }
    files.push_back(arg);
  }
  if (files.empty()) {
    std::cerr << "Usage: rstsonnet [--include-private] [-i N|--indent=N] [-w N|--width=N] <file.jsonnet> [file2.jsonnet ...]\n";
    std::cerr << "Reads Jsonnet files and writes ReStructured Text to stdout.\n";
    std::cerr << "  -i N, --indent=N   Indent for definition list body (default: 3). 0 or -1 = no indent.\n";
    std::cerr << "  -w N, --width=N    Max line length for wrapping (default: 100). 0 or -1 = no wrap.\n";
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
      std::cerr << "rstsonnet: cannot open " << path << "\n";
      return 1;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();
    emit_rest(path, content, std::cout, include_private, indent, width);
    if (i + 1 < files.size())
      std::cout << "\n";
  }
  return 0;
}
