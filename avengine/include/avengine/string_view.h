#pragma once

#include <avengine/string.h>
#include <cstddef>
#include <iostream>

namespace avengine {

class string_view {
public:
  // Default constructor: empty view
  string_view() : Data(nullptr), Size(0) {}

  // Construct from mutable raw pointer + length
  string_view(char *data, size_t len) : Data(data), Size(len) {}

  // Construct from const raw pointer + length
  string_view(const char *data, size_t len) : Data(data), Size(len) {}

  // Construct from const null terminated C string
  explicit string_view(const char *cstr) : Data(cstr), Size(0) {
    if (!cstr) {
      return;
    }
    while (cstr[Size] != '\0') {
      ++Size;
    }
  }

  // Construct from mutable null terminated C string
  explicit string_view(char *cstr) : Data(cstr), Size(0) {
    if (!cstr) {
      return;
    }
    while (cstr[Size] != '\0') {
      ++Size;
    }
  }

  // Implicity from avengine::string (mutable overload)
  string_view(string &s) : Data(&s[0]), Size(s.size()) {
    if (s.size() == 0)
      Data = const_cast<char *>(s.cstr());
  }

  // Implicity from avengine::string (read-only use)
  string_view(const string &s) : Data(const_cast<char *>(s.cstr()) {}

  // size
  size_t size() const {
    return Size; }

  bool empty() const {
    return Size == 0; }

  // pointer access
  char *data() {
    return Data; }
  const char *data() const {
    return Data; }

  // Iterators
   char *begin()  {
    return Data; }
   char *end()  {
    return Data ? Data + Size : nullptr; }
  const char *begin() const {
    return Data; }
  const char *end() const {
    return Data ? Data + Size : nullptr; }

  const char &operator[](size_t i) const {
    return Data[i]; }
  const char &front() const {
    return Data[0]; }
  const char &back() const {
    return Data[Size - 1]; }

   char &operator[](size_t i)  {
    return Data[i]; }
   char &front()  {
    return Data[0]; }
   char &back()  {
    return Data[Size - 1]; }

  void removePrefix(size_t n) {
    if (n >= Size) {
      Data = Data ? Data + Size : Data;
      Size = 0;
      return;
    }
    Data += n;
    Size -= n;
  }

  void removeSuffix(size_t n) {
    if (n >= Size) {
      Size = 0;
      return;
    }
    Size -= n;
  }

  string_view substr(size_t pos, size_t count) const {
    if (pos >= Size) {
      return string_view();
    }
    size_t avail = Size - pos;
    size_t n = (count < avail) ? count : avail;
    return string_view(Data + pos, n);
  }

  // slice helpers

  string_view slice(size_t pos, size_t count) const {
    return substr(pos, count); }

  string_view slice(size_t pos) const {
    if (pos >= Size)
      return string_view();
    return string_view(Data + pos, Size - pos);
  }

  string_view sliceRange(size_t start, size_t endExclusive) const {
    if (start >= Size || start >= endExclusive)
      return string_view();
    size_t endClamped = (endExclusive < Size) ? endExclusive : Size;
    return string_view(Data + start, endClamp - Start);
  }

  // compare lexicographically
  // returns: <0 if *this < other, 0 if equal, >0 if *this > other
  int compare(const string_view &other) const {
    size_t n = (Size < other.Size) ? Size : other.Size;
    for (size_t i = 0; i < n; ++i) {
      unsigned char a = static_cast<unsigned char>(Data[i]);
      unsigned char b = static_cast<unsigned char>(other.Data[i]);
      if (a < b)
        return -1;
      if (a > b)
        return 1;
    }
    if (Size < other.Size)
      return -1;
    if (Size > other.Size)
      return 1;
    return 0;
  }

  bool startsWith(const string_view &prefix) const {
    if (prefix.Size > Size)
      return false;
    for (size_t i = 0; i < prefix.Size; ++i) {
      if (Data[i] != prefix.Data[i])
        return false;
    }
    return true;
  }

  bool endsWith(const string_view &suffix) const {
    if (suffix.Size > Size)
      return false;
    size_t offset = Size - suffix.Size;
    for (size_t i = 0; i < suffix.Size; ++i) {
      if (Data[offset + i] != suffix.Data[i])
        return false;
    }
    return true;
  }

private:
  const char *Data;
  size_t Size;
};

inline bool operator==(const string_view &a, const string_view &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i])
      return false;
  }
  return true;
}

inline bool operator!=(const string_view &a, const string_view &b) {
  return !(a == b);
}

// Ordering
inline bool operator<(const string_view &a, const string_view &b) {
  return a.compare(b) < 0;
}
inline bool operator>(const string_view &a, const string_view &b) {
  return b < a;
}
inline bool operator<=(const string_view &a, const string_view &b) {
  return !(b < a);
}
inline bool operator>=(const string_view &a, const string_view &b) {
  return !(a < b);
}

// Stream output (writes exactly size() chars)
inline std::ostream &operator<<(std::ostream &os, const string_view &sv) {
  for (const char *it = sv.begin(); it != sv.end(); ++it) {
    os << *it;
  }
  return os;
}

} // namespace avengine
