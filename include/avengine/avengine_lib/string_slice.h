// string_slice.h
//
// A non-owning, mutable view over a contiguous range of characters
// (pointer + length).  Semantically similar to a Rust &mut str or a
// std::span<char>: no allocation, no deallocation, no null-terminator
// guarantee.
//
// Lifetime contract
// -----------------
// A string_slice does NOT extend the lifetime of the underlying storage.
// In particular, a slice built from an avengine::string's SSO buffer becomes
// dangling if that string is moved or destroyed, because the SSO buffer lives
// inside the string object itself.  This is the same responsibility as
// std::string_view and must be respected by callers.
//
// Mutability contract
// -------------------
// Constructing a string_slice from a `const char*` (e.g. a string literal)
// stores the pointer as char* for uniformity.  The caller is responsible for
// ensuring the underlying memory is actually writable before using operator[]
// (non-const) or any other mutating operation.  Reading through such a slice
// is always safe.

#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <cassert>
#include <cstddef>
#include <iostream>

namespace avengine
{

class string_slice
{
public:
  // Sentinel
  static const size_t npos = static_cast<size_t>(-1);

  // Default Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Creates an empty slice with ptr_ == nullptr and len_ == 0.
  //          All operations that read characters are safe on an empty slice.
  string_slice() : ptr_(nullptr), len_(0) {}

  // Core Constructor (char*, size_t)
  // REQUIRES: ptr points to at least len readable bytes, or len == 0
  // MODIFIES: *this
  // EFFECTS: Creates a slice over [ptr, ptr + len).  No null terminator is
  //          assumed or required.
  string_slice(char* ptr, size_t len) : ptr_(ptr), len_(len) {}

  // Constructor from avengine::string (non-const)
  // REQUIRES: s remains alive and un-moved for the lifetime of this slice
  // MODIFIES: *this
  // EFFECTS: Creates a mutable slice over the full contents of s.
  //          Uses s.data() so that the SSO buffer is accessible without
  //          const-casting.
  explicit string_slice(string& s) : ptr_(s.data()), len_(s.size()) {}

  // Constructor from avengine::string (non-const), substring view
  // REQUIRES: start + len <= s.size()
  //           s remains alive and un-moved for the lifetime of this slice
  // MODIFIES: *this
  // EFFECTS: Creates a mutable slice over s[start .. start+len).
  string_slice(string& s, size_t start, size_t len)
  {
    assert(start + len <= s.size());
    ptr_ = s.data() + start;
    len_ = len;
  }

  // Constructor from const char*
  // REQUIRES: cstr is null-terminated
  // MODIFIES: *this
  // EFFECTS: Walks cstr to compute its length, then stores the pointer as
  //          char*.  Reading through the slice is always safe.  Writing
  //          through operator[] (non-const) is only safe when the caller
  //          knows the underlying storage is writable (i.e. not a string
  //          literal stored in read-only memory).
  string_slice(const char* cstr)
  {
    size_t len = 0;
    while (cstr[len] != '\0')
    {
      ++len;
    }
    // const_cast is intentional: documented in the file-level contract.
    ptr_ = const_cast<char*>(cstr);
    len_ = len;
  }

  // Size
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of characters in the slice.
  size_t size() const
  {
    return len_;
  }

  // Empty
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if the slice contains zero characters.
  bool empty() const
  {
    return len_ == 0;
  }

  // Mutable Element Access
  // REQUIRES: i < size()
  // MODIFIES: Allows modification of the i'th character
  // EFFECTS: Returns a mutable reference to the i'th character.
  char& operator[](size_t i)
  {
    return ptr_[i];
  }

  // Const Element Access
  // REQUIRES: i < size()
  // MODIFIES: Nothing
  // EFFECTS: Returns a read-only reference to the i'th character.
  const char& operator[](size_t i) const
  {
    return ptr_[i];
  }

  // Mutable Begin
  // REQUIRES: Nothing
  // MODIFIES: Allows mutation through the returned pointer
  // EFFECTS: Returns a mutable pointer to the first character, or nullptr
  //          when the slice is empty.
  char* begin()
  {
    return ptr_;
  }

  // Const Begin
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a read-only pointer to the first character.
  const char* begin() const
  {
    return ptr_;
  }

  // Mutable End
  // REQUIRES: Nothing
  // MODIFIES: Allows mutation through pointers in [begin(), end())
  // EFFECTS: Returns a mutable pointer one past the last character.
  char* end()
  {
    return ptr_ + len_;
  }

  // Const End
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a read-only pointer one past the last character.
  const char* end() const
  {
    return ptr_ + len_;
  }

  // To String
  // REQUIRES: Nothing
  // MODIFIES: Nothing (allocates a new avengine::string)
  // EFFECTS: Returns a newly constructed avengine::string whose contents are
  //          a copy of the characters in this slice.  This is the only safe
  //          path to a null-terminated representation — cstr() is deliberately
  //          absent because a slice has no null-terminator guarantee.
  string toString() const
  {
    string result;
    for (size_t i = 0; i < len_; ++i)
    {
      result.pushBack(ptr_[i]);
    }
    return result;
  }

  // Substr
  // REQUIRES: start + len <= size()
  // MODIFIES: Nothing
  // EFFECTS: Returns a new string_slice that is a narrowed view into the
  //          same underlying buffer starting at offset start with length len.
  //          Mutations through the returned slice are visible in *this (and
  //          the original string) because no copying takes place.
  string_slice substr(size_t start, size_t len) const
  {
    assert(start + len <= len_);
    return string_slice(ptr_ + start, len);
  }

  // Find
  // REQUIRES: sub is null-terminated; pos <= size()
  // MODIFIES: Nothing
  // EFFECTS: Searches for the first occurrence of sub in the slice starting
  //          at position pos.  Returns the zero-based index of the first
  //          match, or npos if sub is not present.  An empty sub always
  //          matches at pos (mirrors avengine::string::find behaviour).
  size_t find(const char* sub, size_t pos = 0) const
  {
    // Compute sub length without constructing a temporary string.
    size_t sub_len = 0;
    while (sub[sub_len] != '\0')
    {
      ++sub_len;
    }

    if (sub_len == 0)
    {
      return pos <= len_ ? pos : npos;
    }

    for (size_t i = pos; i + sub_len <= len_; ++i)
    {
      size_t j = 0;
      while (j < sub_len && ptr_[i + j] == sub[j])
      {
        ++j;
      }
      if (j == sub_len)
      {
        return i;
      }
    }

    return npos;
  }

  // Starts With
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if this slice begins with all of prefix's
  //          characters in order.  An empty prefix always matches.
  bool startsWith(string_slice prefix) const
  {
    if (prefix.len_ > len_)
    {
      return false;
    }
    for (size_t i = 0; i < prefix.len_; ++i)
    {
      if (ptr_[i] != prefix.ptr_[i])
      {
        return false;
      }
    }
    return true;
  }

  // Ends With
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if this slice ends with all of suffix's characters
  //          in order.  An empty suffix always matches.
  bool endsWith(string_slice suffix) const
  {
    if (suffix.len_ > len_)
    {
      return false;
    }
    const size_t offset = len_ - suffix.len_;
    for (size_t i = 0; i < suffix.len_; ++i)
    {
      if (ptr_[offset + i] != suffix.ptr_[i])
      {
        return false;
      }
    }
    return true;
  }

  // Trim Left
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a new string_slice with leading ASCII whitespace
  //          characters removed by advancing the start pointer.  The
  //          underlying buffer is not modified.
  string_slice trimLeft() const
  {
    size_t start = 0;
    while (start < len_ && isAsciiWhitespace(ptr_[start]))
    {
      ++start;
    }
    return string_slice(ptr_ + start, len_ - start);
  }

  // Trim Right
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a new string_slice with trailing ASCII whitespace
  //          characters removed by retreating the length.  The underlying
  //          buffer is not modified.
  string_slice trimRight() const
  {
    size_t end = len_;
    while (end > 0 && isAsciiWhitespace(ptr_[end - 1]))
    {
      --end;
    }
    return string_slice(ptr_, end);
  }

  // Trim
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Equivalent to trimLeft().trimRight() — removes both leading and
  //          trailing ASCII whitespace by narrowing the view.
  string_slice trim() const
  {
    return trimLeft().trimRight();
  }

  // Split
  // REQUIRES: The underlying buffer remains alive for the lifetime of every
  //           string_slice in the returned vector (no copies are made).
  // MODIFIES: Nothing
  // EFFECTS: Partitions this slice on delimiter, returning a vector of
  //          non-owning slices into the same buffer.  Consecutive delimiters
  //          produce empty slices.  A delimiter at the very start or end
  //          produces an empty slice at the corresponding boundary.
  //          Example: "a,,b" split on ',' → ["a", "", "b"]
  vector<string_slice> split(char delimiter) const
  {
    vector<string_slice> result;
    size_t start = 0;
    for (size_t i = 0; i <= len_; ++i)
    {
      if (i == len_ || ptr_[i] == delimiter)
      {
        result.pushBack(string_slice(ptr_ + start, i - start));
        start = i + 1;
      }
    }
    return result;
  }

  // Equality
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff both slices have the same length and all
  //          corresponding characters are equal.
  bool operator==(string_slice other) const
  {
    if (len_ != other.len_)
    {
      return false;
    }
    for (size_t i = 0; i < len_; ++i)
    {
      if (ptr_[i] != other.ptr_[i])
      {
        return false;
      }
    }
    return true;
  }

  // Not-Equality
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff the two slices differ in length or any
  //          corresponding character.
  bool operator!=(string_slice other) const
  {
    return !(*this == other);
  }

  // Less Than
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff *this is lexicographically less than other.
  bool operator<(string_slice other) const
  {
    const size_t n = len_ < other.len_ ? len_ : other.len_;
    for (size_t i = 0; i < n; ++i)
    {
      if (ptr_[i] < other.ptr_[i])
      {
        return true;
      }
      if (ptr_[i] > other.ptr_[i])
      {
        return false;
      }
    }
    return len_ < other.len_;
  }

  // Greater Than
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff *this is lexicographically greater than other.
  bool operator>(string_slice other) const
  {
    return other < *this;
  }

  // Less Than Or Equal
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff *this is lexicographically less than or equal
  //          to other.
  bool operator<=(string_slice other) const
  {
    return !(*this > other);
  }

  // Greater Than Or Equal
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true iff *this is lexicographically greater than or
  //          equal to other.
  bool operator>=(string_slice other) const
  {
    return !(*this < other);
  }

private:
  static bool isAsciiWhitespace(char c)
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  }

  char* ptr_;  // start of the viewed character range (may be nullptr iff len_==0)
  size_t len_; // number of characters in the view
};

// Stream Insertion
// REQUIRES: Nothing
// MODIFIES: os
// EFFECTS: Writes every character in s to os in order.  No null terminator
//          is written.
inline std::ostream& operator<<(std::ostream& os, const string_slice& s)
{
  for (const char* it = s.begin(); it != s.end(); ++it)
  {
    os << *it;
  }
  return os;
}

} // namespace avengine
