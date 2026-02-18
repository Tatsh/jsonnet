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

/** Sanitize a key for use as C++ identifier (namespace/struct/member name). */
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

/** C++ type name for Doxygen. */
const char *cpp_type(const std::string &type) {
  if (type == "number") return "double";
  if (type == "string") return "std::string";
  if (type == "boolean") return "bool";
  if (type == "object") return "mixed";  // nested struct emitted separately
  if (type == "array") return "mixed";
  return "mixed";
}

/** Represents a documented key in the hierarchy. */
struct DocKey {
  std::vector<std::string> path;  // path components (object keys)
  std::string key;                 // this key
  std::string type;                // number, string, boolean, object, array, mixed
  std::string doc;                 // comment block text
};

/** Represents a file-level or scope-level doc block (e.g. @file or namespace description). */
struct DocBlock {
  std::string content;
  bool is_file;
  size_t path_depth;  // 0 = file, 1 = root, 2 = first nested, ...
  std::vector<std::string> path;  // for which path this block applies (if any)
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
  bool in_object = false;  // true when we're inside { } at some level
};

/** Skip whitespace and // line comments. */
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

/** Consume a Doxygen block comment (slash-star-star or slash-star-bang); return true if consumed. */
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
    s.i += 3;  // /*
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

/** Consume a non-doc block comment. */
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

/** Read a string literal "..." or '...' (jsonnet allows both). Returns empty on failure. */
std::string try_string(ScanState &s) {
  if (s.i >= s.src.size())
    return "";
  char quote = s.src[s.i];
  if (quote != '"' && quote != '\'' && quote != '|')
    return "";
  size_t start = s.i;
  s.i++;
  if (quote == '|') {
    // Text block: skip to next |
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

/** Read an identifier [a-zA-Z_][a-zA-Z0-9_]*. */
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

/** Peek value type after ':' (object, array, number, string, boolean, null, mixed). */
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
  return "mixed";  // function, local, expression, etc.
}

/** Skip a value (for brace matching). Only skips one level of { } or [ ]. */
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
    std::string _ = try_string(s);
    return;
  }
  if (c == '|') {
    std::string _ = try_string(s);
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
  // function, local, identifier, etc. - skip until we hit , } ] or newline at same level
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

/** Scan one file and populate file_blocks and keys. */
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

/** True if key or any path segment has a leading underscore (private). */
bool path_or_key_has_underscore_prefix(const std::vector<std::string> &path,
                                      const std::string &key) {
  for (const auto &p : path) {
    if (!p.empty() && p[0] == '_')
      return true;
  }
  return !key.empty() && key[0] == '_';
}

/** True when this key should be skipped by default (no doc, or _ prefix). */
bool should_skip_key(const DocKey &dk, bool include_private) {
  if (include_private)
    return false;
  if (dk.doc.empty())
    return true;
  return path_or_key_has_underscore_prefix(dk.path, dk.key);
}

/** Build scope path string for C++ (e.g. "root::server::port"). */
std::string scope_path(const std::vector<std::string> &path,
                      const std::string &key) {
  std::string s = "root";
  for (const auto &p : path)
    s += "::" + sanitize_id(p);
  if (!key.empty())
    s += "::" + sanitize_id(key);
  return s;
}

/** Emit C++-style Doxygen output for one file (doc blocks only for FILTER_PATTERNS). */
void emit_doxygen(const std::string &filename, const std::string &content,
                  std::ostream &out, bool include_private) {
  ScanState s{content};
  scan_file(s);

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
  for (const DocKey &dk : s.keys) {
    if (!should_skip_key(dk, include_private)) {
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
  for (const DocKey &dk : s.keys) {
    if (should_skip_key(dk, include_private))
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
        std::string ns = scope_path(p, "");
        out << "/** @namespace " << ns << "\n";
        out << " *  Nested object scope.\n";
        out << " */\n\n";
      }
    }

    std::string full_scope = scope_path(dk.path, dk.key);
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
