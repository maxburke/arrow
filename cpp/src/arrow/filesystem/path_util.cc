// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/filesystem/path_util.h"
#include "arrow/result.h"
#include "arrow/status.h"
#include "arrow/util/logging.h"
#include "arrow/util/string_view.h"

namespace arrow {
namespace fs {
namespace internal {

// XXX How does this encode Windows UNC paths?

std::vector<std::string> SplitAbstractPath(const std::string& path) {
  std::vector<std::string> parts;
  auto v = util::string_view(path);
  // Strip trailing slash
  if (v.length() > 0 && v.back() == kSep) {
    v = v.substr(0, v.length() - 1);
  }
  // Strip leading slash
  if (v.length() > 0 && v.front() == kSep) {
    v = v.substr(1);
  }
  if (v.length() == 0) {
    return parts;
  }

  auto append_part = [&parts, &v](size_t start, size_t end) {
    parts.push_back(std::string(v.substr(start, end - start)));
  };

  size_t start = 0;
  while (true) {
    size_t end = v.find_first_of(kSep, start);
    append_part(start, end);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

std::pair<std::string, std::string> GetAbstractPathParent(const std::string& s) {
  // XXX should strip trailing slash?

  auto pos = s.find_last_of(kSep);
  if (pos == std::string::npos) {
    // Empty parent
    return {{}, s};
  }
  return {s.substr(0, pos), s.substr(pos + 1)};
}

std::string GetAbstractPathExtension(const std::string& s) {
  util::string_view basename(s);
  auto offset = basename.find_last_of(kSep);
  if (offset != std::string::npos) {
    basename = basename.substr(offset);
  }
  auto dot = basename.find_last_of('.');
  if (dot == util::string_view::npos) {
    // Empty extension
    return "";
  }
  return basename.substr(dot + 1).to_string();
}

Status ValidateAbstractPathParts(const std::vector<std::string>& parts) {
  for (const auto& part : parts) {
    if (part.length() == 0) {
      return Status::Invalid("Empty path component");
    }
    if (part.find_first_of(kSep) != std::string::npos) {
      return Status::Invalid("Separator in component '", part, "'");
    }
  }
  return Status::OK();
}

std::string ConcatAbstractPath(const std::string& base, const std::string& stem) {
  DCHECK(!stem.empty());
  if (base.empty()) {
    return stem;
  } else if (base.back() == kSep) {
    return base + stem;
  } else {
    return base + kSep + stem;
  }
}

std::string EnsureTrailingSlash(util::string_view v) {
  if (v.length() > 0 && v.back() != kSep) {
    // XXX How about "C:" on Windows?  We probably don't want to turn it into "C:/"...
    // Unless the local filesystem always uses absolute paths
    return std::string(v) + kSep;
  } else {
    return std::string(v);
  }
}

std::string EnsureLeadingSlash(util::string_view v) {
  if (v.length() == 0 || v.front() != kSep) {
    // XXX How about "C:" on Windows?  We probably don't want to turn it into "/C:"...
    return kSep + std::string(v);
  } else {
    return std::string(v);
  }
}
util::string_view RemoveTrailingSlash(util::string_view key) {
  while (!key.empty() && key.back() == kSep) {
    key.remove_suffix(1);
  }
  return key;
}

util::string_view RemoveLeadingSlash(util::string_view key) {
  while (!key.empty() && key.front() == kSep) {
    key.remove_prefix(1);
  }
  return key;
}

Result<std::string> MakeAbstractPathRelative(const std::string& base,
                                             const std::string& path) {
  if (base.empty() || base.front() != kSep) {
    return Status::Invalid("MakeAbstractPathRelative called with non-absolute base '",
                           base, "'");
  }
  auto b = EnsureLeadingSlash(RemoveTrailingSlash(base));
  auto p = util::string_view(path);
  if (p.substr(0, b.size()) != util::string_view(b)) {
    return Status::Invalid("Path '", path, "' is not relative to '", base, "'");
  }
  p = p.substr(b.size());
  if (!p.empty() && p.front() != kSep && b.back() != kSep) {
    return Status::Invalid("Path '", path, "' is not relative to '", base, "'");
  }
  return std::string(RemoveLeadingSlash(p));
}

bool IsAncestorOf(util::string_view ancestor, util::string_view descendant) {
  ancestor = RemoveTrailingSlash(ancestor);
  if (ancestor == "") {
    // everything is a descendant of the root directory
    return true;
  }

  descendant = RemoveTrailingSlash(descendant);
  if (!descendant.starts_with(ancestor)) {
    // an ancestor path is a prefix of descendant paths
    return false;
  }

  descendant.remove_prefix(ancestor.size());

  // "/hello/w" is not an ancestor of "/hello/world"
  return descendant.starts_with(std::string{kSep});
}

util::optional<util::string_view> RemoveAncestor(util::string_view ancestor,
                                                 util::string_view descendant) {
  if (!IsAncestorOf(ancestor, descendant)) {
    return util::nullopt;
  }

  auto relative_to_ancestor = descendant.substr(ancestor.size());
  return RemoveLeadingSlash(relative_to_ancestor);
}

}  // namespace internal
}  // namespace fs
}  // namespace arrow
