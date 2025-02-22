// Copyright 2024, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

extern "C" {
#include "redis/quicklist.h"
}

#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace dfly {

class QList {
 public:
  enum Where { TAIL, HEAD };

  // Provides wrapper around the references to the listpack entries.
  class Entry {
    std::variant<std::string_view, int64_t> value_;

   public:
    Entry(const char* value, size_t length) : value_{std::string_view(value, length)} {
    }

    explicit Entry(int64_t longval) : value_{longval} {
    }

    // Assumes value is not int64.
    std::string_view view() const {
      return std::get<std::string_view>(value_);
    }

    int64_t ival() const {
      return std::get<int64_t>(value_);
    }

    bool operator==(std::string_view sv) const;

    std::string to_string() const {
      if (std::holds_alternative<int64_t>(value_)) {
        return std::to_string(std::get<int64_t>(value_));
      }
      return std::string(view());
    }
  };

  class Iterator {
   public:
    Entry Get() const;

    // Returns false if no more entries.
    bool Next();

   private:
    const QList* owner_ = nullptr;
    quicklistNode* current_ = nullptr;
    unsigned char* zi_ = nullptr; /* points to the current element */
    long offset_ = 0;             /* offset in current listpack */
    uint8_t direction_ = 1;

    friend class QList;
  };

  using IterateFunc = std::function<bool(Entry)>;
  enum InsertOpt { BEFORE, AFTER };

  QList();
  QList(int fill, int compress);
  QList(QList&&);
  QList(const QList&) = delete;
  ~QList();

  QList& operator=(const QList&) = delete;
  QList& operator=(QList&&);

  size_t Size() const {
    return count_;
  }

  void Clear();

  void Push(std::string_view value, Where where);
  void AppendListpack(unsigned char* zl);
  void AppendPlain(unsigned char* zl, size_t sz);

  // Returns true if pivot found and elem inserted, false otherwise.
  bool Insert(std::string_view pivot, std::string_view elem, InsertOpt opt);

  // Returns true if item was replaced, false if index is out of range.
  bool Replace(long index, std::string_view elem);

  size_t MallocUsed() const;

  void Iterate(IterateFunc cb, long start, long end) const;

  // Returns an iterator to tail or the head of the list.
  // To mirror the quicklist interface, the iterator is not valid until Next() is called.
  // TODO: to fix this.
  Iterator GetIterator(Where where) const;

  // Returns an iterator at a specific index 'idx',
  // or Invalid iterator if index is out of range.
  // negative index - means counting from the tail.
  // Requires calling subsequent Next() to initialize the iterator.
  Iterator GetIterator(long idx) const;

  uint32_t noded_count() const {
    return len_;
  }

  Iterator Erase(Iterator it);

 private:
  bool AllowCompression() const {
    return compress_ != 0;
  }

  // Returns false if used existing head, true if new head created.
  bool PushHead(std::string_view value);

  // Returns false if used existing head, true if new head created.
  bool PushTail(std::string_view value);
  void InsertPlainNode(quicklistNode* old_node, std::string_view, InsertOpt insert_opt);
  void InsertNode(quicklistNode* old_node, quicklistNode* new_node, InsertOpt insert_opt);
  void Insert(Iterator it, std::string_view elem, InsertOpt opt);
  void Replace(Iterator it, std::string_view elem);

  void Compress(quicklistNode* node);

  quicklistNode* MergeNodes(quicklistNode* node);
  quicklistNode* ListpackMerge(quicklistNode* a, quicklistNode* b);

  void DelNode(quicklistNode* node);
  bool DelPackedIndex(quicklistNode* node, uint8_t** p);

  quicklistNode* head_ = nullptr;
  quicklistNode* tail_ = nullptr;
  uint32_t count_ = 0;                   /* total count of all entries in all listpacks */
  uint32_t len_ = 0;                     /* number of quicklistNodes */
  signed int fill_ : QL_FILL_BITS;       /* fill factor for individual nodes */
  unsigned int compress_ : QL_COMP_BITS; /* depth of end nodes not to compress;0=off */
  unsigned int bookmark_count_ : QL_BM_BITS;
};

}  // namespace dfly
