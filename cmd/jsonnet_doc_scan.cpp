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

#include "jsonnet_doc_scan.h"

#include <cctype>
#include <sstream>

namespace jsonnet_doc {

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

bool is_file_block(const std::string &block) {
  std::string n = normalize_doc_block(block);
  return n.find("@file") != std::string::npos;
}

static std::string trim_line_for_var(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i < line.size() && line[i] == '*')
    i++;
  if (i < line.size() && line[i] == ' ')
    i++;
  return line.substr(i);
}

std::string extract_var_type(std::string &doc) {
  std::istringstream is(doc);
  std::string line;
  std::string out_doc;
  std::string found_type;
  bool first = true;
  while (std::getline(is, line)) {
    std::string trimmed = trim_line_for_var(line);
    if (found_type.empty() && trimmed.size() >= 5 &&
        trimmed.compare(0, 5, "@var ") == 0) {
      size_t start = 5;
      while (start < trimmed.size() &&
             (trimmed[start] == ' ' || trimmed[start] == '\t'))
        start++;
      size_t end = start;
      while (end < trimmed.size() && trimmed[end] != ' ' && trimmed[end] != '\t')
        end++;
      found_type = trimmed.substr(start, end - start);
      if (found_type == "bool")
        found_type = "boolean";
      continue;
    }
    if (!first)
      out_doc += '\n';
    first = false;
    out_doc += line;
  }
  if (!found_type.empty())
    doc = out_doc;
  return found_type;
}

std::string extract_rv_type(std::string &doc) {
  std::istringstream is(doc);
  std::string line;
  std::string out_doc;
  std::string found_type;
  bool first = true;
  while (std::getline(is, line)) {
    std::string trimmed = trim_line_for_var(line);
    if (found_type.empty() && trimmed.size() >= 4 &&
        trimmed.compare(0, 4, "@rv ") == 0) {
      size_t start = 4;
      while (start < trimmed.size() &&
             (trimmed[start] == ' ' || trimmed[start] == '\t'))
        start++;
      size_t end = start;
      while (end < trimmed.size() && trimmed[end] != ' ' && trimmed[end] != '\t')
        end++;
      found_type = trimmed.substr(start, end - start);
      if (found_type == "bool")
        found_type = "boolean";
      continue;
    }
    if (!first)
      out_doc += '\n';
    first = false;
    out_doc += line;
  }
  if (!found_type.empty())
    doc = out_doc;
  return found_type;
}

std::vector<std::string> extract_pt_types(std::string &doc) {
  std::istringstream is(doc);
  std::string line;
  std::string out_doc;
  std::vector<std::string> found_types;
  bool first = true;
  while (std::getline(is, line)) {
    std::string trimmed = trim_line_for_var(line);
    if (found_types.empty() && trimmed.size() >= 4 &&
        trimmed.compare(0, 4, "@pt ") == 0) {
      size_t pos = 4;
      while (pos < trimmed.size()) {
        while (pos < trimmed.size() &&
               (trimmed[pos] == ' ' || trimmed[pos] == '\t'))
          pos++;
        if (pos >= trimmed.size())
          break;
        size_t start = pos;
        while (pos < trimmed.size() && trimmed[pos] != ',')
          pos++;
        size_t end = pos;
        while (end > start && (trimmed[end - 1] == ' ' || trimmed[end - 1] == '\t'))
          end--;
        if (end > start) {
          std::string t = trimmed.substr(start, end - start);
          if (t == "bool")
            t = "boolean";
          found_types.push_back(t);
        }
        if (pos < trimmed.size())
          pos++;
      }
      continue;
    }
    if (!first)
      out_doc += '\n';
    first = false;
    out_doc += line;
  }
  if (!found_types.empty())
    doc = out_doc;
  return found_types;
}

std::string extract_namespace(std::string &doc) {
  std::istringstream is(doc);
  std::string line;
  std::string out_doc;
  std::string found_name;
  bool first = true;
  while (std::getline(is, line)) {
    std::string trimmed = trim_line_for_var(line);
    if (found_name.empty() && trimmed.size() >= 11 &&
        trimmed.compare(0, 11, "@namespace ") == 0) {
      size_t start = 11;
      while (start < trimmed.size() &&
             (trimmed[start] == ' ' || trimmed[start] == '\t'))
        start++;
      size_t end = start;
      while (end < trimmed.size() && trimmed[end] != ' ' && trimmed[end] != '\t')
        end++;
      found_name = trimmed.substr(start, end - start);
      continue;
    }
    if (!first)
      out_doc += '\n';
    first = false;
    out_doc += line;
  }
  if (!found_name.empty())
    doc = out_doc;
  return found_name;
}

std::string extract_brief(const std::string &doc) {
  std::istringstream is(doc);
  std::string line;
  while (std::getline(is, line)) {
    std::string trimmed = trim_line_for_var(line);
    if (trimmed.size() >= 7 && trimmed.compare(0, 7, "@brief ") == 0) {
      size_t start = 7;
      while (start < trimmed.size() &&
             (trimmed[start] == ' ' || trimmed[start] == '\t'))
        start++;
      return trimmed.substr(start);
    }
    if (trimmed.size() >= 7 && trimmed.compare(0, 7, "\\brief ") == 0) {
      size_t start = 7;
      while (start < trimmed.size() &&
             (trimmed[start] == ' ' || trimmed[start] == '\t'))
        start++;
      return trimmed.substr(start);
    }
  }
  return "";
}

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

static void skip_ws_and_line_comments(ScanState &s) {
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

static bool try_block_comment(ScanState &s, std::string &out) {
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

static void skip_block_comment(ScanState &s) {
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

/** Copy content of '(' ... ')' into out (excluding parens), then skip past ')'.
 *  s.i must be at '(' on entry. Returns true if a group was consumed. */
static bool copy_paren_group(ScanState &s, std::string &out) {
  if (s.i >= s.src.size() || s.src[s.i] != '(')
    return false;
  size_t start = s.i + 1;
  s.i++;
  int depth = 1;
  while (s.i < s.src.size() && depth > 0) {
    char c = s.src[s.i];
    if (c == '"' || c == '\'' || c == '|') {
      if (c == '|') {
        s.i++;
        while (s.i < s.src.size() && s.src[s.i] != '|')
          s.i++;
        if (s.i < s.src.size())
          s.i++;
      } else {
        char q = c;
        s.i++;
        while (s.i < s.src.size() &&
               (s.src[s.i] != q || (s.i > 0 && s.src[s.i - 1] == '\\')))
          s.i++;
        if (s.i < s.src.size())
          s.i++;
      }
      continue;
    }
    if (c == '(')
      depth++;
    else if (c == ')') {
      depth--;
      if (depth == 0) {
        out = s.src.substr(start, s.i - start);
        s.i++;
        return true;
      }
    }
    s.i++;
  }
  return false;
}

static std::string try_string(ScanState &s) {
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

static std::string try_identifier(ScanState &s) {
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

static std::string peek_value_type(ScanState &s) {
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
  if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
    size_t p = s.i;
    if (s.src[p] == '-')
      p++;
    if (p < s.src.size() && s.src[p] == '0' && p + 1 < s.src.size() &&
        (s.src[p + 1] == 'x' || s.src[p + 1] == 'X'))
      return "int";  // hex literal
    bool has_dot = false, has_exp = false;
    while (p < s.src.size() &&
           (std::isdigit(static_cast<unsigned char>(s.src[p])) ||
            s.src[p] == '.' || s.src[p] == 'e' || s.src[p] == 'E' ||
            (p > s.i && (s.src[p] == '+' || s.src[p] == '-')))) {
      if (s.src[p] == '.')
        has_dot = true;
      else if (s.src[p] == 'e' || s.src[p] == 'E')
        has_exp = true;
      p++;
    }
    return (has_dot || has_exp) ? "float" : "int";
  }
  return "mixed";
}

static void skip_value(ScanState &s) {
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
        while (s.i < s.src.size() &&
               (s.src[s.i] != q || (s.i > 0 && s.src[s.i - 1] == '\\')))
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
        while (s.i < s.src.size() &&
               (s.src[s.i] != q || (s.i > 0 && s.src[s.i - 1] == '\\')))
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
    while (s.i < s.src.size() &&
           std::isalpha(static_cast<unsigned char>(s.src[s.i])))
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
        std::string params;
        bool is_function = (s.i < s.src.size() && s.src[s.i] == '(') &&
                           copy_paren_group(s, params);
        if (is_function) {
          size_t p = 0;
          while (p < params.size() && (params[p] == ' ' || params[p] == '\t'))
            p++;
          size_t q = params.size();
          while (q > p && (params[q - 1] == ' ' || params[q - 1] == '\t'))
            q--;
          params = params.substr(p, q - p);
        }
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
          dk.type = is_function ? "function" : value_type;
          dk.doc = s.pending_doc;
          if (is_function)
            dk.function_params = params;
          size_t value_start = s.i;
          if (value_type == "object") {
            skip_ws_and_line_comments(s);
            if (s.i < s.src.size() && s.src[s.i] == '{') {
              s.i++;
              s.brace_depth++;
              s.path.push_back(key);
            } else {
              skip_value(s);
              if (!is_function)
                dk.value_verbatim = s.src.substr(value_start, s.i - value_start);
            }
          } else {
            skip_value(s);
            if (!is_function)
              dk.value_verbatim = s.src.substr(value_start, s.i - value_start);
          }
          s.keys.push_back(dk);
          s.pending_doc.clear();
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

std::string scope_path(const std::vector<std::string> &path,
                       const std::string &key) {
  std::string out = "root";
  for (const auto &p : path)
    out += "::" + sanitize_id(p);
  if (!key.empty())
    out += "::" + sanitize_id(key);
  return out;
}

void wrap_lines(std::ostream &out, const std::string &text, int width,
                int first_indent, int cont_indent) {
  std::istringstream is(text);
  std::string para;
  while (std::getline(is, para)) {
    if (width <= 0) {
      out << std::string(first_indent, ' ') << para << '\n';
      continue;
    }
    const int max_first = width - first_indent;
    const int max_cont = width - cont_indent;
    if (max_first <= 0 && max_cont <= 0) {
      out << std::string(first_indent, ' ') << para << '\n';
      continue;
    }
    int col = first_indent;
    out << std::string(first_indent, ' ');
    std::istringstream ws(para);
    std::string word;
    bool first_word = true;
    while (ws >> word) {
      int need = static_cast<int>(word.size());
      if (!first_word)
        need += 1;
      if (col > first_indent && col + need > width) {
        out << '\n' << std::string(cont_indent, ' ');
        col = cont_indent;
        need = static_cast<int>(word.size());
      }
      if (col > first_indent)
        out << ' ';
      out << word;
      col += need;
      first_word = false;
    }
    out << '\n';
  }
}

}  // namespace jsonnet_doc
