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

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "jsonnet_doc_scan.h"

namespace {

std::string cpp_type(const std::string &type) {
  if (type == "number") return "double";
  if (type == "boolean") return "bool";
  if (type == "function" || type == "object" || type == "array") return "mixed";
  return type;
}

/** Strip line (// and #) and block (slash-star star-slash) comments from Jsonnet source; leaves string literals unchanged. */
static std::string strip_comments_from_jsonnet(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '"' || s[i] == '\'') {
      char q = s[i];
      out += s[i++];
      while (i < s.size()) {
        if (s[i] == '\\' && i + 1 < s.size()) {
          out += s[i++];
          out += s[i++];
          continue;
        }
        if (s[i] == q) {
          out += s[i++];
          break;
        }
        out += s[i++];
      }
      continue;
    }
    if (s[i] == '|') {
      out += s[i++];
      while (i < s.size() && s[i] != '|')
        out += s[i++];
      if (i < s.size())
        out += s[i++];
      continue;
    }
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
      i += 2;
      while (i < s.size() && s[i] != '\n')
        i++;
      if (i < s.size())
        i++;
      continue;
    }
    if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
        i++;
      if (i + 1 < s.size())
        i += 2;
      if (i < s.size() && s[i] == '\n')
        i++;
      continue;
    }
    if (s[i] == '#') {
      while (i < s.size() && s[i] != '\n')
        i++;
      if (i < s.size())
        i++;
      continue;
    }
    out += s[i++];
  }
  return out;
}

/** Remove leading whitespace so opening and closing braces align; use last line indent as base. */
static std::string normalize_indent_multiline(const std::string &s) {
  if (s.find('\n') == std::string::npos)
    return s;
  std::istringstream is(s);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(is, line))
    lines.push_back(line);
  if (lines.empty())
    return s;
  size_t base = 0;
  {
    const std::string &last = lines.back();
    while (base < last.size() && (last[base] == ' ' || last[base] == '\t'))
      base++;
  }
  if (base == 0)
    return s;
  std::string out;
  for (size_t j = 0; j < lines.size(); ++j) {
    const std::string &ln = lines[j];
    size_t i = 0;
    while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t'))
      i++;
    size_t strip = std::min(base, i);
    if (j != 0)
      out += '\n';
    out += ln.substr(strip);
  }
  return out;
}

/** Escape star-slash and slash-star in comment lines so C++ block comment stays valid. */
static std::string escape_comment_line(const std::string &line) {
  std::string out;
  out.reserve(line.size() + 4);
  for (size_t i = 0; i < line.size(); ++i) {
    if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
      out += "* /";
      i++;
    } else if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
      out += "/ *";
      i++;
    } else {
      out += line[i];
    }
  }
  return out;
}

void emit_doxygen(const std::string &filename, const std::string &content,
                  std::ostream &out, bool include_private) {
  jsonnet_doc::ScanState s{content};
  jsonnet_doc::parse_file_to_doc_state(filename, content, s);

  bool has_file_block = false;
  std::string top_level_namespace = "root";
  std::string file_brief;
  for (const auto &b : s.file_blocks) {
    if (b.is_file) {
      std::string file_doc = b.content;
      std::string ns = jsonnet_doc::extract_namespace(file_doc);
      if (!ns.empty())
        top_level_namespace = jsonnet_doc::sanitize_id(ns);
      file_brief = jsonnet_doc::extract_brief(file_doc);
      out << "/**\n";
      std::istringstream is(file_doc);
      std::string line;
      while (std::getline(is, line))
        out << " * " << escape_comment_line(line) << "\n";
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
  if (!any_visible)
    return;

  std::string line_name = filename;
  for (size_t i = 0; i < line_name.size(); ) {
    if (line_name[i] == '\\') { line_name.insert(i, "\\"); i += 2; continue; }
    if (line_name[i] == '"') { line_name.insert(i, "\\"); i += 2; continue; }
    i++;
  }
  out << "#line 1 \"" << line_name << "\"\n";
  out << "/** @namespace " << top_level_namespace << "\n";
  if (!file_brief.empty())
    out << " *  @brief " << escape_comment_line(file_brief) << "\n";
  else
    out << " *  @brief Namespace for symbols from this file.\n";
  out << " */\n";
  out << "namespace " << top_level_namespace << " {\n\n";
  std::vector<std::string> stack;
  for (const auto &dk : s.keys) {
    if (jsonnet_doc::should_skip_key(dk, include_private))
      continue;
    while (stack.size() > dk.path.size() ||
           (stack.size() == dk.path.size() && stack != dk.path)) {
      out << "}\n\n";
      stack.pop_back();
    }
    std::string name = jsonnet_doc::sanitize_id(dk.key);
    std::string doc_for_comment = dk.doc;
    std::string rv_type;
    std::vector<std::string> pt_types;
    if (dk.type == "function") {
      rv_type = jsonnet_doc::extract_rv_type(doc_for_comment);
      pt_types = jsonnet_doc::extract_pt_types(doc_for_comment);
    }
    if (!doc_for_comment.empty() || (dk.type != "function" && !dk.value_verbatim.empty())) {
      out << "/**\n";
      if (!doc_for_comment.empty()) {
        std::istringstream is(doc_for_comment);
        std::string line;
        while (std::getline(is, line))
          out << " * " << escape_comment_line(line) << "\n";
      }
      if (dk.type != "function" && !dk.value_verbatim.empty()) {
        std::string code_value = strip_comments_from_jsonnet(dk.value_verbatim);
        code_value = normalize_indent_multiline(code_value);
        bool multiline = code_value.find('\n') != std::string::npos;
        out << " * <dl class=\"section default-value\">\n";
        out << " * <dt>Default value</dt>\n";
        out << " * <dd>\n";
        out << " * @code\n";
        if (multiline) {
          std::istringstream vs(code_value);
          std::string vline;
          while (std::getline(vs, vline))
            out << " * " << escape_comment_line(vline) << "\n";
        } else {
          out << " * "
              << escape_comment_line(code_value)
              << "\n";
        }
        out << " * @endcode\n";
        out << " * </dd>\n";
        out << " * </dl>\n";
      }
      out << " */\n";
    }
    if (dk.type == "object") {
      out << "namespace " << name << " {\n\n";
      stack.push_back(dk.key);
    } else if (dk.type == "function") {
      out << cpp_type(rv_type.empty() ? "mixed" : rv_type) << " " << name << "(";
      std::istringstream ps(dk.function_params);
      std::string p;
      bool first = true;
      size_t param_idx = 0;
      while (std::getline(ps, p, ',')) {
        size_t start = 0;
        while (start < p.size() && (p[start] == ' ' || p[start] == '\t'))
          start++;
        size_t end = p.size();
        while (end > start && (p[end - 1] == ' ' || p[end - 1] == '\t'))
          end--;
        p = p.substr(start, end - start);
        if (!p.empty()) {
          if (!first)
            out << ", ";
          std::string param_type =
              param_idx < pt_types.size() ? pt_types[param_idx] : "mixed";
          out << cpp_type(param_type) << " " << jsonnet_doc::sanitize_id(p);
          first = false;
          param_idx++;
        }
      }
      out << ");\n\n";
    } else {
      out << cpp_type(dk.type) << " " << name << ";\n\n";
    }
  }
  while (!stack.empty()) {
    out << "}\n\n";
    stack.pop_back();
  }
  out << "}\n\n";  // close top-level namespace
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
