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

#pragma once

#include "arrow/filesystem/filesystem.h"

#include <algorithm>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "arrow/result.h"
#include "arrow/status.h"
#include "arrow/util/compare.h"
#include "arrow/util/macros.h"
#include "arrow/util/sort.h"

namespace arrow {
namespace fs {

/// \brief A PathForest is a utility to transform a vector of FileStats into a
/// forest representation for tree traversal purposes. Note: there is no guarantee of a
/// shared root. Each node in the graph wraps a FileStats. Files are expected to be found
/// only at leaves of the tree.
class ARROW_EXPORT PathForest : public util::EqualityComparable<PathForest> {
 public:
  /// \brief Transforms a FileStats vector into a forest. The caller should ensure that
  /// stats does not contain duplicates.
  ///
  /// Vector(s) of associated objects (IE associated[i] is associated with stats[i]) may
  /// be passed for reordering. (After construction, associated[i] is associated with
  /// forest[i]).
  template <typename... Associated>
  static Result<PathForest> Make(std::vector<FileStats> stats,
                                 std::vector<Associated>*... associated) {
    auto compare_paths = [](const FileStats& lhs, const FileStats& rhs) {
      return lhs.path() < rhs.path();
    };

    if (sizeof...(associated) == 0) {
      std::sort(stats.begin(), stats.end(), compare_paths);
    } else {
      auto indices = arrow::internal::ArgSort(stats, compare_paths);
      size_t _[] = {arrow::internal::Permute(indices, &stats),
                    arrow::internal::Permute(indices, associated)...};
      static_cast<void>(_);
    }

    return DoMake(std::move(stats));
  }

  /// \brief Returns the number of nodes in this forest.
  int size() const { return size_; }

  bool Equals(const PathForest& other) const;

  std::string ToString() const;

  /// Reference to a node in the forest
  struct ARROW_EXPORT Ref {
    const FileStats& stats() const;

    int num_descendants() const;

    /// Returns a PathForest containing only nodes which are descendants of this node
    PathForest descendants() const;

    /// This node's parent or Ref{nullptr, 0} if this node has no parent
    Ref parent() const;

    explicit operator bool() const { return forest != NULLPTR; }

    const PathForest* forest;
    int i;
  };

  Ref operator[](int i) const { return Ref{this, i}; }

  std::vector<Ref> roots() const;

  enum { Continue, Prune };
  using MaybePrune = Result<decltype(Prune)>;

  /// Visitors may return MaybePrune to enable eager pruning. Visitors will be called with
  /// the FileStats of the currently visited node and the index of that node in depth
  /// first visitation order (useful for accessing parallel vectors of associated data).
  template <typename Visitor>
  Status Visit(Visitor&& v) const {
    static std::is_same<decltype(v(std::declval<Ref>())), MaybePrune> with_pruning;
    return VisitImpl(std::forward<Visitor>(v), with_pruning);
  }

 protected:
  PathForest(int offset, int size, std::shared_ptr<std::vector<FileStats>> stats,
             std::shared_ptr<std::vector<int>> descendant_counts,
             std::shared_ptr<std::vector<int>> parents)
      : offset_(offset),
        size_(size),
        stats_(std::move(stats)),
        descendant_counts_(std::move(descendant_counts)),
        parents_(std::move(parents)) {}

  static Result<PathForest> DoMake(std::vector<FileStats> sorted_stats);

  /// \brief Visit with eager pruning.
  template <typename Visitor>
  Status VisitImpl(Visitor&& v, std::true_type) const {
    for (int i = 0; i < size_; ++i) {
      Ref ref = {this, i};
      ARROW_ASSIGN_OR_RAISE(auto action, v(ref));

      if (action == Prune) {
        // skip descendants
        i += ref.num_descendants();
      }
    }

    return Status::OK();
  }

  template <typename Visitor>
  Status VisitImpl(Visitor&& v, std::false_type) const {
    return Visit([&](Ref ref) -> MaybePrune {
      ARROW_RETURN_NOT_OK(v(ref));
      return Continue;
    });
  }

  int offset_, size_;
  std::shared_ptr<std::vector<FileStats>> stats_;
  std::shared_ptr<std::vector<int>> descendant_counts_, parents_;
};

ARROW_EXPORT std::ostream& operator<<(std::ostream& os, const PathForest& tree);

}  // namespace fs
}  // namespace arrow
