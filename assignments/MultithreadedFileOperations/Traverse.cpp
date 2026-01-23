// Traverse.cpp
//
// Create and print a sorted list of all the paths specified
// on the command line, traversing any directories to add
// their contents using a pool of worker threads.  Ignore
// any non-existent paths.
// 
// Compile with g++ --std=c++17 Traverse.cpp -pthread -o Traverse


#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>


using namespace std;


//		YOUR CODE HERE



// Get the next path to be traversed from the work queue
// shared among all the threads, sleeping if the queue is
// empty. If this is the last thread going to sleep, signal
// that all the work is done.


string GetWork( )
   {

	//		YOUR CODE HERE


   }


// Add  work to the queue and signal that there's work available.


void AddWork( string path )
   {

	//		YOUR CODE HERE


   }


// Add a new path to the list all those that have been found,


void AddPath( string path )
	{

	//		YOUR CODE HERE


	}


bool DotName( const char *name )
   {
   return name[ 0 ] == '.' &&
         ( name[ 1 ] == 0 || name[ 1 ] == '.' && name[ 2 ] == 0 );
   }


// Traverse a path.  If it exists, add it to the list of paths that
// have been found.  If it's a directory, add any children other than
// . and .. to the work queue.  If it doesn't exist, ignore it.


void *Traverse( string pathname )
   {

	//		YOUR CODE HERE


	}


// Each worker thread simply loops, grabbing the next item
// on the work queue and traversing it.


void *WorkerThread( void *arg )
	{
	while ( true )
		Traverse( GetWork( ) );

	// Never reached.
	return nullptr;				
	}


// main() should do the following.
// 
// 1. Initialize the locks.
// 2. Iterate over the argv pathnames, adding them to the work queue.
// 3. Create the specified number of workers.
// 4. Sleep until the work has finished.
// 5. Sort the paths found vector.
// 6  Print the list of paths.


int main( int argc, char **argv )
   {
   if ( argc < 3  || ( ThreadCount = atoi( argv[ 1 ] ) ) == 0 )
      {
      cerr <<	"Usage: Traverse <number of workers> <list of pathnames>" << endl <<
					"Number of workers must be greater than 0." << endl <<
					"Invalid paths are ignored." << endl;
      return 1;
      }


	//		YOUR CODE HERE

	return 0;
   }
