// string.h
//
// Starter file for a string template
#pragma once

#include <avengine/avengine_lib/vector.h>
#include <cstddef>  // for size_t
#include <iostream> // for ostream
#include <utility>

namespace avengine
{

class string
{
public:
  // Default Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Creates an empty string
  string()
  {
    initSmallEmpty();
  }

  // string Literal / C string Constructor
  // REQUIRES: cstr is a null terminated C style string
  // MODIFIES: *this
  // EFFECTS: Creates a string with equivalent contents to cstr
  string(const char* cstr)
  {
    initSmallEmpty();
    size_t len = 0;
    while (cstr[len] != '\0')
    {
      ++len;
    }
    assignFrom(cstr, len);
  }

  // Copy Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Creates a copy of other
  string(const string& other)
  {
    initSmallEmpty();
    assignFrom(other.cstr(), other.size());
  }

  // Copy Assignment
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Copies contents of other into *this
  string& operator=(const string& other)
  {
    if (this != &other)
    {
      assignFrom(other.cstr(), other.size());
    }
    return *this;
  }

  // Move Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this, leave other in default constructed state
  string(string&& other)
  {
    if (other.isSmall())
    {
      initSmallEmpty();
      const size_t otherSize = other.size();
      for (size_t i = 0; i < otherSize; ++i)
      {
        Data.Small.Buffer[i] = other.Data.Small.Buffer[i];
      }
      setSmallSize(otherSize);
    }
    else
    {
      Data.Heap.Ptr = other.Data.Heap.Ptr;
      Data.Heap.Size = other.Data.Heap.Size;
      Data.Heap.CapAndTag = other.Data.Heap.CapAndTag;
      other.initSmallEmpty();
    }
  }

  // Move Assignment
  // REQUIRES: Nothing
  // MODIFES: *this, leaves other in default constructed state
  string& operator=(string&& other)
  {
    if (this != &other)
    {
      if (!isSmall())
      {
        delete[] Data.Heap.Ptr;
      }

      if (other.isSmall())
      {
        initSmallEmpty();
        const size_t otherSize = other.size();
        for (size_t i = 0; i < otherSize; ++i)
        {
          Data.Small.Buffer[i] = other.Data.Small.Buffer[i];
        }
        setSmallSize(otherSize);
      }
      else
      {
        Data.Heap.Ptr = other.Data.Heap.Ptr;
        Data.Heap.Size = other.Data.Heap.Size;
        Data.Heap.CapAndTag = other.Data.Heap.CapAndTag;
      }

      other.initSmallEmpty();
    }
    return *this;
  }

  // Destructor
  // REQUIRES: Nothing
  // MODIFES: Destroys *this
  // EFFECTS: Frees heap storage when active
  ~string()
  {
    if (!isSmall())
    {
      delete[] Data.Heap.Ptr;
    }
  }

  // Size
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of characters in the string
  size_t size() const
  {
    return isSmall() ? static_cast<size_t>(Data.Small.Size) : Data.Heap.Size;
  }

  // C string Conversion
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a pointer to a null terminated C string of *this
  const char* cstr() const
  {
    if (!isSmall())
    {
      return Data.Heap.Ptr;
    }

    if (Data.Small.Size < SSO_CAP)
    {
      return Data.Small.Buffer;
    }

    static thread_local char fullSmallBuffer[SSO_CAP + 1];
    for (size_t i = 0; i < SSO_CAP; ++i)
    {
      fullSmallBuffer[i] = Data.Small.Buffer[i];
    }
    fullSmallBuffer[SSO_CAP] = '\0';
    return fullSmallBuffer;
  }

  // Data (mutable)
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a mutable pointer to the first character in the string's
  //          storage.  The caller is responsible for not writing past size()
  //          bytes and for keeping this string alive for the lifetime of any
  //          pointer derived from this accessor.  In particular, a string_slice
  //          built from data() becomes dangling if the string is moved or
  //          destroyed, because the SSO buffer lives inside *this.
  char* data()
  {
    return isSmall() ? Data.Small.Buffer : Data.Heap.Ptr;
  }

  // Iterator Begin
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to the start of the string
  const char* begin() const
  {
    return isSmall() ? Data.Small.Buffer : Data.Heap.Ptr;
  }

  // Iterator End
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to the end of the string
  const char* end() const
  {
    return begin() + size();
  }

  // Element Access
  // REQUIRES: 0 <= i < size()
  // MODIFIES: Allows modification of the i'th element
  // EFFECTS: Returns the i'th character of the string
  char& operator[](size_t i)
  {
    return isSmall() ? Data.Small.Buffer[i] : Data.Heap.Ptr[i];
  }

  // Const Element Access
  const char& operator[](size_t i) const
  {
    return isSmall() ? Data.Small.Buffer[i] : Data.Heap.Ptr[i];
  }

  // string Append
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Appends the contents of other to *this, resizing any
  //      memory at most once
  void operator+=(const string& other)
  {
    if (this == &other)
    {
      string tmp(other);
      *this += tmp;
      return;
    }

    const size_t selfSize = size();
    const size_t otherLen = other.size();
    if (otherLen == 0)
    {
      return;
    }

    const size_t newSize = selfSize + otherLen;
    const char* otherData = other.cstr();

    // 1: Stay in small mode
    if (isSmall() && newSize <= SSO_CAP)
    {
      for (size_t i = 0; i < otherLen; ++i)
      {
        Data.Small.Buffer[selfSize + i] = otherData[i];
      }
      setSmallSize(newSize);
      return;
    }

    // 2: need heap
    if (isSmall())
    {
      size_t newCap = newSize;
      char* newPtr = new char[newCap + 1];
      for (size_t i = 0; i < selfSize; ++i)
      {
        newPtr[i] = Data.Small.Buffer[i];
      }
      for (size_t i = 0; i < otherLen; ++i)
      {
        newPtr[selfSize + i] = otherData[i];
      }
      newPtr[newSize] = '\0';
      Data.Heap.Ptr = newPtr;
      Data.Heap.Size = newSize;
      Data.Heap.CapAndTag = encodeHeapCap(newCap);
      return;
    }

    reserveHeap(newSize);
    for (size_t i = 0; i < otherLen; ++i)
    {
      Data.Heap.Ptr[selfSize + i] = otherData[i];
    }
    Data.Heap.Size = newSize;
    Data.Heap.Ptr[newSize] = '\0';
  }

  size_t find(const char* sub, size_t pos = 0) const
  {
    size_t source_len = this->size();
    size_t sub_len = avengine::string(sub).size();

    if (sub_len == 0)
    {
      return pos <= source_len ? pos : avengine::string::npos;
    }

    for (size_t i = pos; i + sub_len <= source_len; ++i)
    {
      size_t j = 0;
      while (j < sub_len && (*this)[i + j] == sub[j])
      {
        ++j;
      }
      if (j == sub_len)
      {
        return i; // Found at index i
      }
    }

    return avengine::string::npos; // Not found
  }

  // Push Back
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Appends c to the string
  void pushBack(char c)
  {
    const size_t oldSize = size();
    if (isSmall())
    {
      if (oldSize < SSO_CAP)
      {
        Data.Small.Buffer[oldSize] = c;
        setSmallSize(oldSize + 1);
        return;
      }

      const size_t newSize = oldSize + 1;
      char* newPtr = new char[newSize + 1];
      for (size_t i = 0; i < oldSize; ++i)
      {
        newPtr[i] = Data.Small.Buffer[i];
      }
      newPtr[oldSize] = c;
      newPtr[newSize] = '\0';
      Data.Heap.Ptr = newPtr;
      Data.Heap.Size = newSize;
      Data.Heap.CapAndTag = encodeHeapCap(newSize);
      return;
    }

    reserveHeap(oldSize + 1);
    Data.Heap.Ptr[oldSize] = c;
    Data.Heap.Size = oldSize + 1;
    Data.Heap.Ptr[oldSize + 1] = '\0';
  }

  // Pop Back
  // REQUIRES: string is not empty
  // MODIFIES: *this
  // EFFECTS: Removes the last charater of the string
  void popBack()
  {
    const size_t oldSize = size();
    if (oldSize == 0)
    {
      return;
    }

    if (isSmall())
    {
      setSmallSize(oldSize - 1);
      return;
    }

    Data.Heap.Size = oldSize - 1;
    Data.Heap.Ptr[oldSize - 1] = '\0';
  }

  // Equality Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether all the contents of *this
  //    and other are equal
  bool operator==(const string& other) const
  {

    if (size() != other.size())
    {
      return false;
    }
    for (size_t i = 0; i < size(); ++i)
    {
      if ((*this)[i] != other[i])
      {
        return false;
      }
    }
    return true;
  }

  // Not-Equality Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether at least one character differs between
  //    *this and other
  bool operator!=(const string& other) const
  {
    return !(*this == other);
  }

  // Less Than Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether *this is lexigraphically less than other
  bool operator<(const string& other) const
  {
    const size_t selfSize = size();
    const size_t otherSize = other.size();
    size_t n = (selfSize < otherSize) ? selfSize : otherSize;
    for (size_t i = 0; i < n; ++i)
    {
      char a = (*this)[i];
      char b = other[i];
      if (a < b)
        return true;
      if (a > b)
        return false;
    }
    return selfSize < otherSize;
  }

  // Greater Than Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether *this is lexigraphically greater than other
  bool operator>(const string& other) const
  {
    return other < *this;
  }

  // Less Than Or Equal Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether *this is lexigraphically less or equal to
  // other
  bool operator<=(const string& other) const
  {
    return !(*this > other);
  }

  // Greater Than Or Equal Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether *this is lexigraphically less or equal to
  // other
  bool operator>=(const string& other) const
  {
    return !(*this < other);
  }

  // Split
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Splits *this into a vector of strings based on delimiter.
  //          Returns a vector containing the substrings between delimiters.
  //          Consecutive delimiters result in empty strings.
  vector<string> split(char delimiter) const
  {
    vector<string> result;
    string current;

    for (size_t i = 0; i < size(); ++i)
    {
      if ((*this)[i] == delimiter)
      {
        result.pushBack(current);
        current = string();
      }
      else
      {
        current.pushBack((*this)[i]);
      }
    }
    result.pushBack(current);

    return result;
  }

  const static size_t npos = static_cast<size_t>(-1);

private:
  // SSO capacity
  // TODO: what sso capacity do we want
  // NOTE: SSO being allocated to char array vs vector
  enum
  {
    SSO_CAP = 23
  };

  // Helper to assign from raw char buffer (assumes no '\0' at end)
  void assignFrom(const char* s, size_t len)
  {
    if (!isSmall())
    {
      delete[] Data.Heap.Ptr;
    }

    if (len <= SSO_CAP)
    {
      for (size_t i = 0; i < len; ++i)
      {
        Data.Small.Buffer[i] = s[i];
      }
      setSmallSize(len);
      return;
    }

    Data.Heap.Ptr = new char[len + 1];
    for (size_t i = 0; i < len; ++i)
    {
      Data.Heap.Ptr[i] = s[i];
    }
    Data.Heap.Ptr[len] = '\0';
    Data.Heap.Size = len;
    Data.Heap.CapAndTag = encodeHeapCap(len);
  }

  static constexpr size_t HEAP_TAG_BIT = size_t(1) << ((sizeof(size_t) * 8) - 1);

  struct HeapRep
  {
    char* Ptr;
    size_t Size;
    size_t CapAndTag;
  };

  struct SmallRep
  {
    char Buffer[SSO_CAP];
    unsigned char Size;
  };

  union Storage
  {
    HeapRep Heap;
    SmallRep Small;
  } Data;

  static size_t encodeHeapCap(size_t capacity)
  {
    return capacity | HEAP_TAG_BIT;
  }

  static size_t decodeHeapCap(size_t capAndTag)
  {
    return capAndTag & ~HEAP_TAG_BIT;
  }

  bool isSmall() const
  {
    return (Data.Small.Size & 0x80u) == 0;
  }

  void initSmallEmpty()
  {
    Data.Small.Size = 0;
    Data.Small.Buffer[0] = '\0';
  }

  void setSmallSize(size_t len)
  {
    Data.Small.Size = static_cast<unsigned char>(len);
    if (len < SSO_CAP)
    {
      Data.Small.Buffer[len] = '\0';
    }
  }

  size_t heapCapacity() const
  {
    return decodeHeapCap(Data.Heap.CapAndTag);
  }

  void reserveHeap(size_t minCapacity)
  {
    size_t currentCapacity = heapCapacity();
    if (minCapacity <= currentCapacity)
    {
      return;
    }

    size_t newCapacity = currentCapacity == 0 ? 1 : currentCapacity;
    while (newCapacity < minCapacity)
    {
      newCapacity *= 2;
    }

    char* newPtr = new char[newCapacity + 1];
    for (size_t i = 0; i < Data.Heap.Size; ++i)
    {
      newPtr[i] = Data.Heap.Ptr[i];
    }
    newPtr[Data.Heap.Size] = '\0';
    delete[] Data.Heap.Ptr;
    Data.Heap.Ptr = newPtr;
    Data.Heap.CapAndTag = encodeHeapCap(newCapacity);
  }
};

static_assert(sizeof(string) == sizeof(void*) * 3,
              "avengine::string should be three pointers wide");

inline std::ostream& operator<<(std::ostream& os, const string& s)
{
  for (const char* it = s.begin(); it != s.end(); ++it)
  {
    os << *it;
  }
  return os;
}
} // namespace avengine
