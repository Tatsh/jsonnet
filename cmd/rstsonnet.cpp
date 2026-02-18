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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

namespace {

/** Normalize a Doxygen comment block (strip leading * and whitespace from lines). */
std::string normalize_doc_block(std::string block) {
  std::string out;
  std::istringstream is(block);
  std::string line;
  bool first = true;
  while (std::getline(is, line)) {
    if (!first)
      out += '\n';
    first = false;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
      i++;
    if (i < line.size() && line[i] == '*')
      i++;
    if (i < line.size() && line[i] == ' ')
      i++;
    out += line.substr(i);
  }
  return out;
}

/** Check if block looks like a file-level @file block. */
bool is_file_block(const std::string &block) {
  std::string n = normalize_doc_block(block);
  return n.find("@file") != std::string::npos;
}

/** Sanitize a key for use in ReST (safe as section title or term). */
std::string sanitize_id(const std::string &key) {
  std::string out;
  for (size_t i = 0; i < key.size(); i++) {
    char c = key[i];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
      out += c;
    else if (out.empty() || out.back() != '_')
      out += '_';
  }
  if (out.empty())
    out = "unnamed";
  if (std::isdigit(static_cast<unsigned char>(out[0])))
    out = "_" + out;
  return out;
}

/** Type name for ReST. */
const char *rest_type(const std::string &type) {
  if (type == "number") return "number";
  if (type == "string") return "string";
  if (type == "boolean") return "boolean";
  if (type == "object") return "object";
  if (type == "array") return "array";
  return "mixed";
}

/** Represents a documented key in the hierarchy. */
struct DocKey {
  std::vector<std::string> path;
  std::string key;
  std::string type;
  std::string doc;
};

struct DocBlock {
  std::string content;
  bool is_file;
  size_t path_depth;
  std::vector<std::string> path;
};

/** Simple scanner state for jsonnet structure (no full parse). */
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

void skip_ws_and_line_comments(ScanState &s) {
  while (s.i < s.src.size()) {
    if (s.src[s.i] == '/' && s.i + 1 < s.src.size() && s.src[s.i + 1] == '/') {
      s.i += 2;
      while (s.i < s.src.size() && s.src[s.i] != '\n')
        s.i++;
      continue;
    }
    if (s.src[s.i] == ' ' || s.src[s.i] == '\t' || s.src[s.i] == '\n' ||
        s.src[s.i] == '\r') {
      s.i++;
      continue;
    }
    break;
  }
}

bool try_block_comment(ScanState &s, std::string &out) {
  if (s.i + 2 >= s.src.size())
    return false;
  const char *p = &s.src[s.i];
  if ((p[0] != '/' || (p[1] != '*' && p[1] != '!')) &&
      (p[0] != '/' || p[1] != '*' || (p[2] != '*' && p[2] != '!')))
    return false;
  size_t start = s.i;
  if (p[1] == '!')
    s.i += 3;
  else
    s.i += 3;
  out.clear();
  while (s.i + 1 < s.src.size()) {
    if (s.src[s.i] == '*' && s.src[s.i + 1] == '/') {
      s.i += 2;
      out += s.src.substr(start, s.i - 2 - start);
      if (out.size() >= 2 && out[0] == '/' && out[1] == '*')
        out = out.substr(2);
      else if (out.size() >= 3 && out[0] == '/' && out[1] == '*' && out[2] == '!')
        out = out.substr(3);
      return true;
    }
    s.i++;
  }
  return false;
}

void skip_block_comment(ScanState &s) {
  if (s.i + 2 < s.src.size() && s.src[s.i] == '/' && s.src[s.i + 1] == '*') {
    s.i += 2;
    while (s.i + 1 < s.src.size()) {
      if (s.src[s.i] == '*' && s.src[s.i + 1] == '/') {
        s.i += 2;
        return;
      }
      s.i++;
    }
  }
}

std::string try_string(ScanState &s) {
  if (s.i >= s.src.size())
    return "";
  char quote = s.src[s.i];
  if (quote != '"' && quote != '\'' && quote != '|')
    return "";
  size_t start = s.i;
  s.i++;
  if (quote == '|') {
    while (s.i < s.src.size()) {
      if (s.src[s.i] == '|') {
        s.i++;
        return s.src.substr(start + 1, s.i - 1 - start - 1);
      }
      s.i++;
    }
    return "";
  }
  while (s.i < s.src.size()) {
    if (s.src[s.i] == '\\') {
      s.i += 2;
      continue;
    }
    if (s.src[s.i] == quote) {
      s.i++;
      return s.src.substr(start + 1, s.i - 1 - start - 1);
    }
    s.i++;
  }
  return "";
}

std::string try_identifier(ScanState &s) {
  if (s.i >= s.src.size())
    return "";
  if (!std::isalpha(static_cast<unsigned char>(s.src[s.i])) &&
      s.src[s.i] != '_')
    return "";
  size_t start = s.i;
  s.i++;
  while (s.i < s.src.size() &&
         (std::isalnum(static_cast<unsigned char>(s.src[s.i])) ||
          s.src[s.i] == '_'))
    s.i++;
  return s.src.substr(start, s.i - start);
}

std::string peek_value_type(ScanState &s) {
  skip_ws_and_line_comments(s);
  if (s.i >= s.src.size())
    return "mixed";
  char c = s.src[s.i];
  if (c == '{')
    return "object";
  if (c == '[')
    return "array";
  if (c == '"' || c == '\'' || c == '|')
    return "string";
  if (c == 't' || c == 'f') {
    if (s.src.substr(s.i, 4) == "true" || s.src.substr(s.i, 5) == "false")
      return "boolean";
    return "mixed";
  }
  if (c == 'n' && s.i + 4 <= s.src.size() && s.src.substr(s.i, 4) == "null")
    return "mixed";
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
    return "number";
  return "mixed";
}

void skip_value(ScanState &s) {
  skip_ws_and_line_comments(s);
  if (s.i >= s.src.size())
    return;
  char c = s.src[s.i];
  if (c == '{') {
    s.i++;
    unsigned depth = 1;
    while (s.i < s.src.size() && depth > 0) {
      skip_ws_and_line_comments(s);
      if (s.i >= s.src.size())
        break;
      if (s.src[s.i] == '"' || s.src[s.i] == '\'') {
        char q = s.src[s.i];
        s.i++;
        while (s.i < s.src.size() && (s.src[s.i] != q || (s.i > 0 && s.src[s.i - 1] == '\\')))
          s.i++;
        if (s.i < s.src.size())
          s.i++;
        continue;
      }
      if (s.src[s.i] == '|') {
        s.i++;
        while (s.i < s.src.size() && s.src[s.i] != '|')
          s.i++;
        if (s.i < s.src.size())
          s.i++;
        continue;
      }
      if (s.src[s.i] == '{')
        depth++;
      else if (s.src[s.i] == '}')
        depth--;
      else if (s.src[s.i] == '/') {
        if (s.i + 1 < s.src.size() && s.src[s.i + 1] == '/') {
          while (s.i < s.src.size() && s.src[s.i] != '\n')
            s.i++;
          continue;
        }
        if (s.i + 1 < s.src.size() && s.src[s.i + 1] == '*') {
          s.i += 2;
          while (s.i + 1 < s.src.size() &&
                 !(s.src[s.i] == '*' && s.src[s.i + 1] == '/'))
            s.i++;
          if (s.i + 1 < s.src.size())
            s.i += 2;
          continue;
        }
      }
      s.i++;
    }
    return;
  }
  if (c == '[') {
    s.i++;
    unsigned depth = 1;
    while (s.i < s.src.size() && depth > 0) {
      skip_ws_and_line_comments(s);
      if (s.i >= s.src.size())
        break;
      if (s.src[s.i] == '"' || s.src[s.i] == '\'') {
        char q = s.src[s.i];
        s.i++;
        while (s.i < s.src.size() && (s.src[s.i] != q || (s.i > 0 && s.src[s.i - 1] == '\\')))
          s.i++;
        if (s.i < s.src.size())
          s.i++;
        continue;
      }
      if (s.src[s.i] == '[')
        depth++;
      else if (s.src[s.i] == ']')
        depth--;
      s.i++;
    }
    return;
  }
  if (c == '"' || c == '\'') {
    (void)try_string(s);
    return;
  }
  if (c == '|') {
    (void)try_string(s);
    return;
  }
  if (c == 't' || c == 'f' || c == 'n') {
    while (s.i < s.src.size() && std::isalpha(static_cast<unsigned char>(s.src[s.i])))
      s.i++;
    return;
  }
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
    if (c == '-')
      s.i++;
    while (s.i < s.src.size() &&
           (std::isdigit(static_cast<unsigned char>(s.src[s.i])) ||
            s.src[s.i] == '.' || s.src[s.i] == 'e' || s.src[s.i] == 'E' ||
            s.src[s.i] == '+' || s.src[s.i] == '-'))
      s.i++;
    return;
  }
  while (s.i < s.src.size()) {
    skip_ws_and_line_comments(s);
    if (s.i >= s.src.size())
      break;
    char c2 = s.src[s.i];
    if (c2 == ',' || c2 == '}' || c2 == ']' || c2 == ';')
      break;
    if (c2 == '{' || c2 == '[') {
      skip_value(s);
      continue;
    }
    if (c2 == '"' || c2 == '\'' || c2 == '|') {
      try_string(s);
      continue;
    }
    s.i++;
  }
}

void scan_file(ScanState &s) {
  std::string block;
  while (s.i < s.src.size()) {
    skip_ws_and_line_comments(s);
    if (s.i >= s.src.size())
      break;
    if (s.src[s.i] == '/' && s.i + 2 < s.src.size()) {
      if (s.src[s.i + 1] == '*') {
        if (s.src[s.i + 2] == '*' || s.src[s.i + 2] == '!') {
          if (try_block_comment(s, block)) {
            s.pending_doc = normalize_doc_block(block);
            s.pending_doc_is_file = is_file_block(block);
            continue;
          }
        } else {
          skip_block_comment(s);
          continue;
        }
      }
    }
    if (s.src[s.i] == '{') {
      s.brace_depth++;
      s.i++;
      if (s.brace_depth == 1) {
        if (s.pending_doc_is_file && !s.pending_doc.empty()) {
          DocBlock db;
          db.content = s.pending_doc;
          db.is_file = true;
          db.path_depth = 0;
          s.file_blocks.push_back(db);
        }
        s.pending_doc_is_file = false;
      }
      s.in_object = true;
      continue;
    }
    if (s.src[s.i] == '}') {
      if (s.brace_depth > 0) {
        s.brace_depth--;
        if (!s.path.empty())
          s.path.pop_back();
      }
      s.i++;
      continue;
    }
    if (s.src[s.i] == '[') {
      s.i++;
      unsigned depth = 1;
      while (s.i < s.src.size() && depth > 0) {
        if (s.src[s.i] == '[')
          depth++;
        else if (s.src[s.i] == ']')
          depth--;
        s.i++;
      }
      continue;
    }
    if (s.brace_depth > 0 && s.in_object) {
      std::string key;
      std::string key_raw;
      if (s.src[s.i] == '"' || s.src[s.i] == '\'') {
        key_raw = try_string(s);
        key = key_raw;
      } else {
        key_raw = try_identifier(s);
        key = key_raw;
      }
      if (!key_raw.empty()) {
        skip_ws_and_line_comments(s);
        bool has_colon = false;
        if (s.i < s.src.size() && s.src[s.i] == ':') {
          has_colon = true;
          s.i++;
        } else if (s.i + 1 < s.src.size() && s.src[s.i] == ':' &&
                   s.src[s.i + 1] == ':') {
          has_colon = true;
          s.i += 2;
        } else if (s.i + 1 < s.src.size() && s.src[s.i] == '+' &&
                   s.src[s.i + 1] == ':') {
          has_colon = true;
          s.i += 2;
        }
        if (has_colon) {
          std::string value_type = peek_value_type(s);
          DocKey dk;
          dk.path = s.path;
          dk.key = key;
          dk.type = value_type;
          dk.doc = s.pending_doc;
          s.keys.push_back(dk);
          s.pending_doc.clear();
          if (value_type == "object") {
            skip_ws_and_line_comments(s);
            if (s.i < s.src.size() && s.src[s.i] == '{') {
              s.i++;
              s.brace_depth++;
              s.path.push_back(key);
            } else {
              skip_value(s);
            }
          } else {
            skip_value(s);
          }
          continue;
        }
      }
    }
    s.pending_doc.clear();
    s.pending_doc_is_file = false;
    s.i++;
  }
}

bool path_or_key_has_underscore_prefix(const std::vector<std::string> &path,
                                      const std::string &key) {
  for (const auto &p : path) {
    if (!p.empty() && p[0] == '_')
      return true;
  }
  return !key.empty() && key[0] == '_';
}

bool should_skip_key(const DocKey &dk, bool include_private) {
  if (include_private)
    return false;
  if (dk.doc.empty())
    return true;
  return path_or_key_has_underscore_prefix(dk.path, dk.key);
}

/** ReST section underline characters by depth (docutils convention). */
static const char REST_UNDERLINES[] = "=-^\"#";

void emit_rest(const std::string &filename, const std::string &content,
               std::ostream &out, bool include_private) {
  ScanState s{content};
  scan_file(s);

  std::string file_title = filename;
  std::string file_intro;
  for (const auto &b : s.file_blocks) {
    if (b.is_file) {
      std::string n = normalize_doc_block(b.content);
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
      out << line << "\n";
    out << "\n";
  }

  bool any_visible = false;
  for (const DocKey &dk : s.keys) {
    if (!should_skip_key(dk, include_private)) {
      any_visible = true;
      break;
    }
  }
  if (!any_visible)
    return;

  std::vector<std::vector<std::string>> seen_sections;
  for (const DocKey &dk : s.keys) {
    if (should_skip_key(dk, include_private))
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
        std::string title = sanitize_id(dk.path[depth - 1]);
        char ul = REST_UNDERLINES[std::min(depth, sizeof(REST_UNDERLINES) - 1)];
        out << title << "\n";
        out << std::string(title.size(), ul) << "\n\n";
      }
    }

    std::string title = sanitize_id(dk.key);
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
          out << line << "\n";
        out << "\n";
      }
    } else {
      out << title << "\n";
      if (!dk.doc.empty()) {
        std::istringstream is(dk.doc);
        std::string line;
        while (std::getline(is, line))
          out << "  " << line << "\n";
      }
      out << "  **Type:** " << rest_type(dk.type) << "\n\n";
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
      std::cerr << "rstsonnet: unknown option " << arg << "\n";
      return 1;
    }
    files.push_back(arg);
  }
  if (files.empty()) {
    std::cerr << "Usage: rstsonnet [--include-private] <file.jsonnet> [file2.jsonnet ...]\n";
    std::cerr << "Reads Jsonnet files and writes ReStructured Text to stdout.\n";
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
    emit_rest(path, content, std::cout, include_private);
    if (i + 1 < files.size())
      std::cout << "\n";
  }
  return 0;
}
