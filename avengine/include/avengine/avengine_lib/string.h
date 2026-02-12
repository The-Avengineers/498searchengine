// string.h
//
// Starter file for a string template

#include <avengine/vector.h>
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
  string() : Heap(), Size(0), IsSmall(true)
  {
    Small[0] = '\0';
  }

  // string Literal / C string Constructor
  // REQUIRES: cstr is a null terminated C style string
  // MODIFIES: *this
  // EFFECTS: Creates a string with equivalent contents to cstr
  string(const char* cstr) : Heap(), Size(0), IsSmall(true)
  {
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
  string(const string& other) : Heap(), Size(0), IsSmall(true)
  {
    assignFrom(other.cstr(), other.Size);
  }

  // Copy Assignment
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Copies contents of other into *this
  string& operator=(const string& other)
  {
    if (this != &other)
    {
      assignFrom(other.cstr(), other.Size);
    }
    return *this;
  }

  // Move Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this, leave other in default constructed state
  string(string&& other) : Heap(), Size(0), IsSmall(true)
  {
    if (other.IsSmall)
    {
      // just copy the small buffer
      Size = other.Size;
      IsSmall = true;
      for (size_t i = 0; i < Size; ++i)
      {
        Small[i] = other.Small[i];
      }
      Small[Size] = '\0';
    }
    else
    {
      // steal heap
      Heap = std::move(other.Heap);
      Size = other.Size;
      IsSmall = false;
    }
    // reset other
    other.Size = 0;
    other.IsSmall = true;
    other.Small[0] = '\0';
    // NOTE: other.Heap is still valid but unused
  }

  // Move Assignment
  // REQUIRES: Nothing
  // MODIFES: *this, leaves other in default constructed state
  string& operator=(string&& other)
  {
    if (this != &other)
    {
      if (other.IsSmall)
      {
        // discard existing heap contents
        Heap.clear();
        // copy into small buffer
        Size = other.Size;
        IsSmall = true;
        for (size_t i = 0; i < Size; ++i)
        {
          Small[i] = other.Small[i];
        }
        Small[Size] = '\0';
      }
      else
      {
        // steal heap
        Heap = std::move(other.Heap);
        Size = other.Size;
        IsSmall = false;
      }

      // reset other
      other.Size = 0;
      other.IsSmall = true;
      other.Small[0] = '\0';
    }
    return *this;
  }

  // Destructor
  // REQUIRES: Nothing
  // MODIFES: Destroys *this
  // EFFECTS: vector<char> Heap cleans up its own storage
  ~string() = default;

  // Size
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of characters in the string
  size_t size() const
  {
    return Size;
  }

  // C string Conversion
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a pointer to a null terminated C string of *this
  const char* cstr() const
  {
    return IsSmall ? Small : Heap.begin();
  }

  // Iterator Begin
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to the start of the string
  const char* begin() const
  {
    return IsSmall ? Small : Heap.begin();
  }

  // Iterator End
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to the end of the string
  const char* end() const
  {
    return (IsSmall ? Small : Heap.begin()) + Size;
  }

  // Element Access
  // REQUIRES: 0 <= i < size()
  // MODIFIES: Allows modification of the i'th element
  // EFFECTS: Returns the i'th character of the string
  char& operator[](size_t i)
  {
    return IsSmall ? Small[i] : Heap[i];
  }

  // Const Element Access
  const char& operator[](size_t i) const
  {
    return IsSmall ? Small[i] : Heap[i];
  }

  // string Append
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Appends the contents of other to *this, resizing any
  //      memory at most once
  void operator+=(const string& other)
  {
    const size_t otherLen = other.Size;
    if (otherLen == 0)
    {
      return;
    }

    const size_t newSize = Size + otherLen;
    const char* otherData = other.cstr();

    // 1: Stay in small mode
    if (IsSmall && newSize <= SSO_CAP)
    {
      for (size_t i = 0; i < otherLen; ++i)
      {
        Small[Size + i] = otherData[i];
      }
      Size = newSize;
      Small[Size] = '\0';
      return;
    }

    // 2: need heap
    if (IsSmall)
    {
      // small -> heap
      Heap.clear();
      Heap.reserve(newSize + 1);
      // copy current contents
      for (size_t i = 0; i < Size; ++i)
      {
        Heap.pushBack(Small[i]);
      }
      // append other contents
      for (size_t j = 0; j < otherLen; ++j)
      {
        Heap.pushBack(otherData[j]);
      }
      // add terminaotr
      Heap.pushBack('\0');

      Size = newSize;
      IsSmall = false;
      return;
    }

    // 3: already heap
    // NOTE: Heap currently holds (Size + 1) chars with Heap[Size] == '\0'
    Heap.reserve(newSize + 1);

    if (otherLen > 0)
    {
      // overwrite terminator
      Heap[Size] = otherData[0];

      // push remaining
      for (size_t i = 1; i < otherLen; ++i)
      {
        Heap.pushBack(otherData[i]);
      }

      // add new terminator
      Heap.pushBack('\0');

      Size = newSize;
    }
  }

  size_t find(const char *sub, size_t pos = 0) {
    size_t source_len = this->size();
    size_t sub_len = avengine::string(sub).size();

    if (sub_len == 0) {
        return pos <= source_len ? pos : avengine::string::npos;
    }

    for (size_t i = pos; i + sub_len <= source_len; ++i) {
        size_t j = 0;
        while (j < sub_len && (*this)[i + j] == sub[j]) {
            ++j;
        }
        if (j == sub_len) {
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
    if (IsSmall)
    {
      if (Size < SSO_CAP)
      {
        Small[Size] = c;
        ++Size;
        Small[Size] = '\0';
        return;
      }

      // must move to heap
      const size_t newSize = Size + 1;
      Heap.clear();
      Heap.reserve(newSize + 1);
      for (size_t i = 0; i < Size; ++i)
      {
        Heap.pushBack(Small[i]);
      }
      Heap.pushBack(c);
      Heap.pushBack('\0');

      Size = newSize;
      IsSmall = false;
      return;
    }

    Heap.reserve(Size + 2);
    Heap[Size] = c;
    Heap.pushBack('\0');
    ++Size;
  }

  // Pop Back
  // REQUIRES: string is not empty
  // MODIFIES: *this
  // EFFECTS: Removes the last charater of the string
  void popBack()
  {
    if (Size == 0)
    {
      return;
    }

    if (IsSmall)
    {
      --Size;
      Small[Size] = '\0';
      return;
    }

    --Size;
    Heap.popBack();
    Heap[Size] = '\0';
  }

  // Equality Operator
  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns whether all the contents of *this
  //    and other are equal
  bool operator==(const string& other) const
  {

    if (Size != other.Size)
    {
      return false;
    }
    for (size_t i = 0; i < Size; ++i)
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
    size_t n = (Size < other.Size) ? Size : other.Size;
    for (size_t i = 0; i < n; ++i)
    {
      char a = (*this)[i];
      char b = other[i];
      if (a < b)
        return true;
      if (a > b)
        return false;
    }
    return Size < other.Size;
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

    for (size_t i = 0; i < Size; ++i)
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
    SSO_CAP = 15
  };

  // Helper to assign from raw char buffer (assumes no '\0' at end)
  void assignFrom(const char* s, size_t len)
  {
    if (len <= SSO_CAP)
    {
      // use small buffer
      for (size_t i = 0; i < len; ++i)
      {
        Small[i] = s[i];
      }
      Small[len] = '\0';
      Size = len;
      IsSmall = true;

      // NOTE: Heap may contain old unused data
      // TODO: Do we care if we clear heap? Shouldn't matter
      return;
    }

    // use heap
    Heap.clear();
    Heap.reserve(len + 1);
    for (size_t i = 0; i < len; ++i)
    {
      Heap.pushBack(s[i]);
    }
    Heap.pushBack('\0');

    Size = len;
    IsSmall = false;
  }

  // NOTE: all strings null terminated
  // TODO: Not null terminated strings? We are storing len so should be easy?
  vector<char> Heap;
  size_t Size;
  bool IsSmall;
  char Small[SSO_CAP + 1];
  
};

inline std::ostream& operator<<(std::ostream& os, const string& s)
{
  for (const char* it = s.begin(); it != s.end(); ++it)
  {
    os << *it;
  }
  return os;
}
} // namespace avengine
