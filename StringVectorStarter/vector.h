// vector.h
// 
// Starter file for a vector template


template<typename T>
   class vector
   {
   public:

      // Default Constructor
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Constructs an empty vector with capacity 0
      vector( )
         {
         }

      // Destructor
      // REQUIRES: Nothing
      // MODIFIES: Destroys *this
      // EFFECTS: Performs any neccessary clean up operations
      ~vector( )
         {
         }

      // Resize Constructor
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Constructs a vector with size num_elements,
      //    all default constructed
      vector( size_t num_elements )
         {
         }

      // Fill Constructor
      // REQUIRES: Capacity > 0
      // MODIFIES: *this
      // EFFECTS: Creates a vector with size num_elements, all assigned to val
      vector( size_t num_elements, const T &val )
         {
         }

      // Copy Constructor
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Creates a clone of the vector other
      vector( const vector<T> &other )
         {
         }

      // Assignment operator
      // REQUIRES: Nothing
      // MODIFIES: *this
      // EFFECTS: Duplicates the state of other to *this
      vector operator=( const vector<T> &other )
         {
         }

      // Move Constructor
      // REQUIRES: Nothing
      // MODIFIES: *this, leaves other in a default constructed state
      // EFFECTS: Takes the data from other into a newly constructed vector
      vector( vector<T> &&other )
         {
         }

      // Move Assignment Operator
      // REQUIRES: Nothing
      // MODIFIES: *this, leaves otherin a default constructed state
      // EFFECTS: Takes the data from other in constant time
      vector operator=( vector<T> &&other )
         {
         }

      // REQUIRES: new_capacity > capacity( )
      // MODIFIES: capacity( )
      // EFFECTS: Ensures that the vector can contain size( ) = new_capacity
      //    elements before having to reallocate
      void reserve( size_t newCapacity )
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns the number of elements in the vector
      size_t size( ) const
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns the maximum size the vector can attain before resizing
      size_t capacity( ) const
         {
         }

      // REQUIRES: 0 <= i < size( )
      // MODIFIES: Allows modification of data[i]
      // EFFECTS: Returns a mutable reference to the i'th element
      T &operator[ ]( size_t i )
         {
         }

      // REQUIRES: 0 <= i < size( )
      // MODIFIES: Nothing
      // EFFECTS: Get a const reference to the ith element
      const T &operator[ ]( size_t i ) const
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: this, size( ), capacity( )
      // EFFECTS: Appends the element x to the vector, allocating
      //    additional space if neccesary
      void pushBack( const T &x )
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: this, size( )
      // EFFECTS: Removes the last element of the vector,
      //    leaving capacity unchanged
      void popBack( )
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Allows mutable access to the vector's contents
      // EFFECTS: Returns a mutable random access iterator to the 
      //    first element of the vector
      T* begin( )
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Allows mutable access to the vector's contents
      // EFFECTS: Returns a mutable random access iterator to 
      //    one past the last valid element of the vector
      T* end( )
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns a random access iterator to the first element of the vector
      const T* begin( ) const
         {
         }

      // REQUIRES: Nothing
      // MODIFIES: Nothing
      // EFFECTS: Returns a random access iterator to 
      //    one past the last valid element of the vector
      const T* end( ) const
         {
         }

   private:

      //TODO

   };
