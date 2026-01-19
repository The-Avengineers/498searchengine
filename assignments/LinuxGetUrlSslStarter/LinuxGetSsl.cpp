#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <openssl/ssl.h>

#include <iostream>

class ParsedUrl
   {
   public:
      const char *CompleteUrl;
      char *Service, *Host, *Port, *Path;

      ParsedUrl( const char *url )
         {
         // Assumes url points to static text but
         // does not check.

         CompleteUrl = url;

         pathBuffer = new char[ strlen( url ) + 1 ];
         const char *f;
         char *t;
         for ( t = pathBuffer, f = url; *t++ = *f++; )
            ;

         Service = pathBuffer;

         const char Colon = ':', Slash = '/';
         char *p;
         for ( p = pathBuffer; *p && *p != Colon; p++ )
            ;

         if ( *p )
            {
            // Mark the end of the Service.
            *p++ = 0;

            if ( *p == Slash )
               p++;
            if ( *p == Slash )
               p++;

            Host = p;

            for ( ; *p && *p != Slash && *p != Colon; p++ )
               ;

            if ( *p == Colon )
               {
               // Port specified.  Skip over the colon and
               // the port number.
               *p++ = 0;
               Port = +p;
               for ( ; *p && *p != Slash; p++ )
                  ;
               }
            else
               Port = p;

            if ( *p )
               // Mark the end of the Host and Port.
               *p++ = 0;

            // Whatever remains is the Path.
            Path = p;
            }
         else
            Host = Path = p;
         }

      ~ParsedUrl( )
         {
         delete[ ] pathBuffer;
         }

   private:
      char *pathBuffer;
   };

int main( int argc, char **argv )
   {

   if ( argc != 2 )
      {
      std::cerr << "Usage: " << argv[ 0 ] << " url" << std::endl;
      return 1;
      }

   // Parse the URL
   ParsedUrl url( argv[ 1 ] );

   // Get the host address.

   // Create a TCP/IP socket.

   // Connect the socket to the host address.

   // Build an SSL layer and set it to read/write
   // to the socket we've connected.

   // Send a GET message.

   // Read from the socket until there's no more data, copying it to
   // stdout.

   // Close the socket and free the address info structure.

   }
