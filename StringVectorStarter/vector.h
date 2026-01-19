// vector.h
//
// Starter file for a vector template
#include <cstddef>
#include <initializer_list>

template <typename T> class vector {
public:
  // Default Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Constructs an empty vector with capacity 0
  vector() : Data(nullptr), Size(0), Capacity(0) {}

  // Destructor
  // REQUIRES: Nothing
  // MODIFIES: Destroys *this
  // EFFECTS: Performs any neccessary clean up operations
  ~vector() { delete[] Data; }

  // Resize Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Constructs a vector with size num_elements,
  //    all default constructed
  vector(size_t num_elements)
      : Data(nullptr), Size(num_elements), Capacity(num_elements) {
    if (num_elements > 0) {
      Data = new T[num_elements];
    }
  }

  // Fill Constructor
  // REQUIRES: Capacity > 0
  // MODIFIES: *this
  // EFFECTS: Creates a vector with size num_elements, all assigned to val
  vector(size_t num_elements, const T &val)
      : Data(nullptr), Size(0), Capacity(0) {
    if (num_elements > 0) {
      // Allocate storage and fill elements with val
      Data = new T[num_elements];
      for (size_t i = 0; i < num_elements; ++i) {
        Data[i] = val;
      }
      Size = num_elements;
      Capacity = num_elements;
    }
  }

  // Initializer List Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Constructs a vector with contents of init
  vector(std::initializer_list<T> init) : Data(nullptr), Size(0), Capacity(0) {
    reserve(init.size());
    for (const T &x : init) {
      pushBack(x);
    }
  }

  // Copy Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Creates a clone of the vector other
  vector(const vector<T> &other)
      : Data(nullptr), Size(other.Size), Capacity(other.Size) {
    // allocate storage + copy element by element
    if (Size > 0) {
      Data = new T[Size];
      for (size_t i = 0; i < Size; ++i) {
        Data[i] = other.Data[i];
      }
    }
  }

  // Assignment operator
  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Duplicates the state of other to *this
  vector operator=(const vector<T> &other) {
    // Check if already equal
    if (this != &other) {
      // Allocate storage + fill
      T *newData = nullptr;
      if (other.Size > 0) {
        newData = new T[other.Size];
        for (size_t i = 0; i < other.Size; ++i) {
          newData[i] = other.Data[i];
        }
      }

      // Free old + reassign
      delete[] Data;
      Data = newData;
      Size = other.Size;
      Capacity = other.Size;
    }
    return *this;
  }

  // Move Constructor
  // REQUIRES: Nothing
  // MODIFIES: *this, leaves other in a default constructed state
  // EFFECTS: Takes the data from other into a newly constructed vector
  vector(vector<T> &&other)
      : Data(other.Data), Size(other.Size), Capacity(other.Capacity) {
    // Clear other data after move
    other.Data = nullptr;
    other.Size = 0;
    other.Capacity = 0;
  }

  // Move Assignment Operator
  // REQUIRES: Nothing
  // MODIFIES: *this, leaves otherin a default constructed state
  // EFFECTS: Takes the data from other in constant time
  vector operator=(vector<T> &&other) {
    if (this != &other) {
      delete[] Data;

      // Copy data over
      Data = other.Data;
      Size = other.Size;
      Capacity = other.Capacity;

      // Clear other data
      other.Data = nullptr;
      other.Size = 0;
      other.Capacity = 0;
    }
    return *this;
  }

  // REQUIRES: new_capacity > capacity( )
  // MODIFIES: capacity( )
  // EFFECTS: Ensures that the vector can contain size( ) = new_capacity
  //    elements before having to reallocate
  void reserve(size_t newCapacity) {
    if (newCapacity <= Capacity) {
      return;
    }

    T *newData = new T[newCapacity];
    // copy existing elements
    for (size_t i = 0; i < Size; ++i) {
      newData[i] = Data[i];
    }

    delete[] Data;
    Data = newData;
    Capacity = newCapacity;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of elements in the vector
  size_t size() const { return Size; }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the maximum size the vector can attain before resizing
  size_t capacity() const { return Capacity; }

  // REQUIRES: 0 <= i < size( )
  // MODIFIES: Allows modification of data[i]
  // EFFECTS: Returns a mutable reference to the i'th element
  T &operator[](size_t i) { return Data[i]; }

  // REQUIRES: 0 <= i < size( )
  // MODIFIES: Nothing
  // EFFECTS: Get a const reference to the ith element
  const T &operator[](size_t i) const { return Data[i]; }

  // REQUIRES: Nothing
  // MODIFIES: this, size( ), capacity( )
  // EFFECTS: Appends the element x to the vector, allocating
  //    additional space if neccesary
  void pushBack(const T &x) {
    // Check if resize needed
    if (Size == Capacity) {
      size_t newCapacity = (Capacity == 0) ? 1 : (Capacity * 2);
      reserve(newCapacity);
    }
    Data[Size] = x;
    ++Size;
  }

  // REQUIRES: Nothing
  // MODIFIES: this, size( )
  // EFFECTS: Removes the last element of the vector,
  //    leaving capacity unchanged
  void popBack() {
    if (Size > 0) {
      --Size;
    }
  }

  // REQUIRES: Nothing
  // MODIFIES: Allows mutable access to the vector's contents
  // EFFECTS: Returns a mutable random access iterator to the
  //    first element of the vector
  T *begin() { return Data; }

  // REQUIRES: Nothing
  // MODIFIES: Allows mutable access to the vector's contents
  // EFFECTS: Returns a mutable random access iterator to
  //    one past the last valid element of the vector
  T *end() { return Data + Size; }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to the first element of the
  // vector
  const T *begin() const { return Data; }
  // NOTE: TEST to make sure const

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns a random access iterator to
  //    one past the last valid element of the vector
  const T *end() const { return Data + Size; }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns True if empty, False otherwise
  bool empty() const { return Size == 0; }

  // REQUIRES: Nothing
  // MODIFIES: this, size
  // EFFECTS: Clears all data from vector, sets size to 0
  void clear() { Size = 0; }

  // REQUIRES: size() > 0
  // EFFECTS: Returns reference to first element
  T &front() { return Data[0]; }
  const T &front() const { return Data[0]; }

  // REQUIRES: size() > 0
  //  EFFECTS: Returns reference to last element
  T &back() { return Data[Size - 1]; }
  const T &back() const { return Data[Size - 1]; }

  // REQUIRES: Nothing
  // MODIFIES: *this, other
  // EFFECTS: Swaps the contents of *this and other
  void swap(vector &other) noexcept {
    T *tmpData = Data;
    Data = other.Data;
    other.Data = tmpData;

    size_t tmpSize = Size;
    Size = other.Size;
    other.Size = tmpSize;

    size_t tmpCapacity = Capacity;
    Capacity = other.Capacity;
    other.Capacity = tmpCapacity;
  }

  // REQUIRES: 0 <= pos <= size()
  // MODIFIES: *this
  // EFFECTS: Inserts value at index pos, elements at [pos, size()) are shifted
  void insert(size_t pos, const T &value) {
    if (pos > Size) {
      pos = Size;
    }

    if (Size == Capacity) {
      size_t newCapacity = (Capacity == 0) ? 1 : (Capacity * 2);
      reserve(newCapacity);
    }

    // Shift elements one step to the right
    for (size_t i = Size; i > pos; --i) {
      Data[i] = Data[i - 1];
    }

    Data[pos] = value;
    ++Size;
  }

  // REQUIRES: 0 <= pos < size()
  // MODIFIES: *this
  // EFFECTS: Removes element at pos, shifts [pos+1, size()) left by one
  void erase(size_t pos) {
    if (pos >= Size) {
      return;
    }

    // Shift left
    for (size_t i = pos; i + 1 < Size; ++i) {
      Data[i] = Data[i + 1];
    }

    --Size;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns index of first element equal to value, or size if not
  // found
  size_t find(const T &value) const {
    for (size_t i = 0; i < Size; ++i) {
      if (Data[i] == value) {
        return i;
      }
    }

    return Size;
  }

private:
  T *Data;
  size_t Size;
  size_t Capacity;
};
