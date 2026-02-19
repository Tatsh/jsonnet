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

#include <sstream>
#include <string>
#include <vector>

#include "core/ast.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/static_error.h"
#include "core/unicode.h"

namespace jsonnet_doc {

namespace {

using namespace jsonnet::internal;

/** Return true if line is // or # style (should be ignored for doc extraction). */
static bool is_line_or_hash_comment(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= line.size())
    return false;
  if (line[i] == '/')
    return i + 1 < line.size() && line[i + 1] == '/';
  return line[i] == '#';
}

/** Extract normalized doc block from fodder (block comments only; ignore // and #). */
static std::string doc_from_fodder(const Fodder &fodder) {
  std::string raw;
  for (const auto &el : fodder) {
    for (const auto &line : el.comment) {
      if (!line.empty() && !is_line_or_hash_comment(line))
        raw += line + "\n";
    }
  }
  if (raw.empty())
    return "";
  size_t start = 0;
  if (raw.size() >= 3 && raw[0] == '/' && raw[1] == '*') {
    start = (raw[2] == '*' || raw[2] == '!') ? 3 : 2;
  } else if (raw.size() >= 2 && raw[0] == '/' && raw[1] == '/')
    start = 2;
  size_t end = raw.size();
  while (end > start && (raw[end - 1] == '\n' || raw[end - 1] == ' '))
    end--;
  if (end > 2 && raw[end - 2] == '*' && raw[end - 1] == '/')
    end -= 2;
  std::string block = raw.substr(start, end - start);
  return normalize_doc_block(block);
}

/** True if fodder looks like a Doxygen-style block (slash-star-star or slash-star-bang). */
static bool fodder_is_doc_block(const Fodder &fodder) {
  if (fodder.empty())
    return false;
  for (const auto &el : fodder) {
    for (const auto &line : el.comment) {
      if (line.size() >= 3 && line[0] == '/' && line[1] == '*' &&
          (line[2] == '*' || line[2] == '!'))
        return true;
    }
  }
  return false;
}

/** Infer value type from expression for doc emission. */
static std::string value_type_from_expr(AST *expr) {
  if (!expr)
    return "mixed";
  switch (expr->type) {
    case AST_OBJECT:
      return "object";
    case AST_ARRAY:
      return "array";
    case AST_LITERAL_BOOLEAN:
      return "boolean";
    case AST_LITERAL_STRING:
      return "string";
    case AST_LITERAL_NULL:
      return "mixed";
    case AST_LITERAL_NUMBER: {
      const auto *n = static_cast<LiteralNumber *>(expr);
      const std::string &s = n->originalString;
      bool has_dot = false, has_exp = false;
      size_t i = (s.size() > 0 && s[0] == '-') ? 1 : 0;
      if (i + 2 <= s.size() && s[i] == '0' &&
          (s[i + 1] == 'x' || s[i + 1] == 'X'))
        return "int";
      for (; i < s.size(); ++i) {
        if (s[i] == '.')
          has_dot = true;
        else if (s[i] == 'e' || s[i] == 'E')
          has_exp = true;
      }
      return (has_dot || has_exp) ? "float" : "int";
    }
    case AST_FUNCTION:
      return "function";
    default:
      return "mixed";
  }
}

/** Comma-separated parameter names from ArgParams. */
static std::string params_from_arg_params(const ArgParams &params) {
  std::ostringstream out;
  bool first = true;
  for (const auto &p : params) {
    if (p.id) {
      if (!first)
        out << ", ";
      out << encode_utf8(p.id->name);
      first = false;
    }
  }
  return out.str();
}

/** Get the object field key name (for FIELD_ID, FIELD_STR, LOCAL). */
static std::string field_key_name(const ObjectField &f) {
  if (f.id)
    return encode_utf8(f.id->name);
  if (f.kind == ObjectField::FIELD_STR && f.expr1) {
    auto *lit = dynamic_cast<LiteralString *>(f.expr1);
    if (lit)
      return encode_utf8(lit->value);
  }
  return "";
}

/** Unwrap top-level LOCAL(s) to get the root Object for doc extraction. */
static AST *doc_root_object(AST *ast) {
  while (ast && ast->type == AST_LOCAL) {
    ast = static_cast<Local *>(ast)->body;
  }
  return (ast && ast->type == AST_OBJECT) ? ast : nullptr;
}

/** Return the substring of content covered by loc (line/column 1-based). */
static std::string location_range_to_source(const std::string &content,
                                            const LocationRange &loc) {
  if (!loc.isSet() || content.empty())
    return "";
  std::vector<size_t> line_starts;
  line_starts.push_back(0);
  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == '\n')
      line_starts.push_back(i + 1);
  }
  if (loc.begin.line == 0 || loc.begin.line > line_starts.size())
    return "";
  size_t start_offset =
      line_starts[loc.begin.line - 1] + (loc.begin.column > 0 ? loc.begin.column - 1 : 0);
  size_t end_offset;
  if (loc.end.line == 0 || loc.end.line > line_starts.size())
    end_offset = content.size();
  else
    end_offset =
        line_starts[loc.end.line - 1] + (loc.end.column > 0 ? loc.end.column - 1 : 0);
  if (start_offset >= end_offset || start_offset >= content.size())
    return "";
  if (end_offset > content.size())
    end_offset = content.size();
  return content.substr(start_offset, end_offset - start_offset);
}

/** Recursively collect DocKeys from an object; path is the current path. */
static void collect_object_fields(AST *ast, std::vector<std::string> &path,
                                  std::vector<DocBlock> &file_blocks,
                                  std::vector<DocKey> &keys, bool top_level,
                                  const std::string &content) {
  if (!ast || ast->type != AST_OBJECT)
    return;
  auto *obj = static_cast<Object *>(ast);
  if (top_level && fodder_is_doc_block(obj->openFodder)) {
    std::string doc = doc_from_fodder(obj->openFodder);
    if (is_file_block(doc)) {
      DocBlock db;
      db.content = doc;
      db.is_file = true;
      db.path_depth = 0;
      db.path = {};
      file_blocks.push_back(db);
    }
  }
  for (const auto &field : obj->fields) {
    std::string key = field_key_name(field);
    if (key.empty()) {
      if (field.kind == ObjectField::ASSERT)
        continue;
      if (field.kind == ObjectField::FIELD_EXPR)
        continue;
      if (field.kind == ObjectField::LOCAL && !field.id)
        continue;
    }

    std::string type =
        field.methodSugar ? "function" : value_type_from_expr(field.expr2);
    std::string doc = doc_from_fodder(field.fodder1);
    std::string var_type = extract_var_type(doc);
    if (!var_type.empty())
      type = var_type;
    std::string function_params =
        field.methodSugar ? params_from_arg_params(field.params) : "";

    DocKey dk;
    dk.path = path;
    dk.key = key;
    dk.type = type;
    dk.doc = doc;
    dk.function_params = function_params;
    if (!field.methodSugar && type != "function" && field.expr2 &&
        field.expr2->location.isSet()) {
      dk.value_verbatim = location_range_to_source(content, field.expr2->location);
    }
    keys.push_back(dk);

    if (type == "object" && field.expr2 && field.expr2->type == AST_OBJECT) {
      path.push_back(key);
      collect_object_fields(field.expr2, path, file_blocks, keys, false, content);
      path.pop_back();
    }
  }
}

}  // namespace

void parse_file_to_doc_state(const std::string &filename,
                             const std::string &content, ScanState &s) {
  try {
    Tokens tokens = jsonnet_lex(filename, content.c_str());
    Allocator alloc;
    AST *ast = jsonnet_parse(&alloc, tokens);
    if (!ast)
      return;
    AST *root_obj = doc_root_object(ast);
    if (!root_obj)
      return;
    std::vector<std::string> path;
    collect_object_fields(root_obj, path, s.file_blocks, s.keys, true, content);
  } catch (const StaticError &) {
    return;
  }
}

}  // namespace jsonnet_doc
