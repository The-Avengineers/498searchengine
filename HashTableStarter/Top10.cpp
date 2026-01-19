// Given a hashtable, find the top 10 elements.

// Nicole Hamilton  nham@umich.edu

#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include "HashTable.h"
#include "Common.h"
#include "Timer.h"
#include "TopN.h"

using namespace std;


void Usage( )
   {
   cout <<

      "Usage:  Top10 [ -vL ] wordsin.txt [ N ]\n"
      "\n"
      "Builds a hash of all the words read from words.txt and then\n"
      "reports the top N most frequent words.  Default is 10 words.\n"
      "\n"
      "-v means verbose output\n"
      "-L means read whole lines as words\n";

   exit( 0 );
   }


using Hash = HashTable< const char *, size_t >;
using Pair = Tuple< const char *, size_t >;

int N = 10;

void PrintTopN( Hash *hashtable )
   {
   Pair **topN, *p;

   if ( optVerbose )
      {
      Timer time;

      cout << "Finding the top " << N << endl;

      time.Start( );
      topN = TopN( hashtable, N );
      time.Finish( );

      time.PrintElapsed( );
      }
   else
      topN = TopN( hashtable, N );

   // Print the top N.

   for ( int i = 0;  i < N && ( p = topN[ i ] );  i++ )
      cout << p->value << "   " << p->key << endl;

   delete [ ] topN;
   }


int main( int argc, char **argv )
   {
   if ( argc < 2 )
      Usage( );

   vector< string > words;

   int i = CollectWordsIn( argc, argv, words );
   if ( ++i < argc )
      N = atoi( argv[ i ] );

   Hash *hashtable = BuildHashTable( words );
   hashtable->Optimize( );

   PrintTopN( hashtable );

   delete hashtable;
   }