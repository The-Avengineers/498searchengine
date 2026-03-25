// RobotsTxt.cpp
//
// Nicole Hamilton, nham@umich.edu

// This is a starter file for an implementation of the RobotsTxt class
// described in RobotsTxt.h.
// 
// The RobotsTxt class can compile a robots.txt file and then determine whether
// access is allowed or disallowed for a given user agent and URL or path.  It
// will also extract any Sitemaps.

// This starter file a little more "paint by numbers" than I'd have liked but I
// wanted to keep the task reasonable.  And, it's only a suggestion. You do not
// have to use any of this code to pass the Autograder, so long as your
// implementation matches the interface definition in RobotsTxt.h.


#include <avengine/crawler/RobotsTxt.h>
#include <cctype>
#include <cstring>
#include <cstdlib>

namespace avengine
{
namespace crawler
{


//
// Utilities
//


inline Utf8 LowerAscii(Utf8 c) {
   return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

inline bool LowerEqual(Utf8 a, Utf8 b) {
   return LowerAscii(a) == LowerAscii(b);
}

class ParsedUrl
   {
   private:
       Utf8 *pathBuffer;

   public:
      const Utf8  *CompleteUrl;
      Utf8        *Service,
                  *Host,
                  *Port,
                  *Path;

      ParsedUrl( const Utf8 *url )
         {
         // Assumes url points to static text but
         // does not check.

         CompleteUrl = url;

         pathBuffer = new Utf8[ strlen( ( const char * )url ) + 1 ];
         const Utf8 *f;
         Utf8 *t;
         for ( t = pathBuffer, f = url;  *t++ = *f++; )
            ;

         Service = pathBuffer;

         const Utf8 Colon = ':', Slash = '/';
         Utf8 *p;
         for ( p = pathBuffer;  *p && *p != Colon;  p++ )
            ;

         if ( *p )
            {
            // Mark the end of the Service.
            *p++ = 0;

            if (*p == Slash)
               p++;
            if (*p == Slash)
               p++;

            Host = p;

            for ( ;  *p && *p != Slash && *p != Colon;  p++ )
               ;

            if ( *p == Colon )
               {
               // Port specified.  Skip over the colon and
               // the port number.
               *p++ = 0;
               Port = p;
               for ( ; *p && *p != Slash;  p++ )
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
         delete [ ] pathBuffer;
         }
   };


int HexLiteralCharacter( char c )
   {
   // If c contains the Ascii code for a hex character, return the
   // binary value; otherwise, -1.

   int i;

   if ( '0' <= c && c <= '9' )
      i = c - '0';
   else
      if ( 'a' <= c && c <= 'f' )
         i = c - 'a' + 10;
      else
         if ( 'A' <= c && c <= 'F' )
            i = c - 'A' + 10;
         else
            i = -1;

   return i;
   }


Utf8 *UrlDecode( const Utf8 *path )
   {
   // Unencode any %xx encodings of characters in the path but
   // this variation leaves %2f "/" encodings unchanged.

   // (Unencoding can only shorten a string or leave it unchanged.
   // It never gets longer.)

   // Caller responsibility to delete [ ] the returned string.

   Utf8 *decode = new Utf8[ strlen( ( const char * )path ) + 1 ], *t = decode, c, d;

   while ( ( c = *path++ ) != 0 )
      if ( c == '%' )
         {
         c = *path;
         if ( c )
            {
            d = *++path;
            if ( d )
               {
               int i, j, hex;
               i = HexLiteralCharacter( c );
               j = HexLiteralCharacter( d );
               hex = ( i << 4 | j );
               if ( hex >= 0 && hex != '/' )
                  {
                  path++;
                  *t++ = ( Utf8 )hex;
                  }
               else
                  {
                  // If the two characters following the %
                  // aren't both hex digits, or if it's a %2f
                  // encoding of a /, treat as literal text.

                  *t++ = '%';
                  path--;
                  }
               }
            }
         }
      else
         *t++ = c;

   // Terminate the string.
   *t = 0;

   return decode;
   }



//
// Robots.txt syntax
//


// <robots.txt> ::= <Line> { <Line > }
// <Line>  ::= { <blank line> | '#" <abitrary text> | <Rule> | <Sitemap> } <LineEnd>
// <Rule> ::= <UserAgent> { <UserAgent> } <Directive> { <Directive> }
// <UserAgent> ::= "User-agent:" <name>
// <Directive> ::= <Permission> <path> | <CrawlDelay>
// <Permission> = "Allow:" | "Disallow:"
// <wildcard> ::= <a string that may contain *>
// <Crawl-delay> ::= "Crawl-delay:" <integer>
// <Sitemap> ::= "Sitemap:" <path>



//
// Tokenizing
//


const Utf8  UserAgentString[ ]   = "user-agent:",
            AllowString[]        = "allow:",
            DisallowString[ ]    = "disallow:",
            CrawlDelayString[ ]  = "crawl-delay:",
            SitemapString[ ]     = "sitemap:";

enum StatementType
   {
   InvalidStatement = 0,
   UserAgentStatement,
   AllowStatement,
   DisallowStatement,
   CrawlDelayStatement,
   SitemapStatement
   };



bool MatchKeyword( const Utf8 *keyword, const Utf8 **start, const Utf8 *bound )
   {
   // Try to match and consume this keyword from the input pointed to by
   // *start.  If there's a match, advance the start pointer to the next input
   // character after the matched keyword and return true.
   //
   // keyword is null-terminated string already in lower-case to be matched
   // case-independent against the start of the input text.

   // YOUR CODE HERE.
   const Utf8 *s = *start;
   if ( s >= bound ) {
      // already at end, so no keyword here
      return false;
   }

   // Compute keyword length once and ensure enough remaining bytes.
   size_t klen = 0;
   while ( keyword[ klen ] ) {
      // count keyword bytes once
      ++klen;
   }

   //if there aren't enough then they obviously don't match
   if ( ( size_t )( bound - s ) < klen ) {
      // not enough bytes left to match
      return false;
   }

   //check each char by char
   for ( size_t i = 0;  i < klen;  i++ ) {
      if ( !LowerEqual( keyword[ i ], s[ i ] ) ) {
         // first mismatch means no match
         return false;
      }
   }

   *start = s + klen;
   return true;
   }


StatementType Identify( const Utf8 **start, const Utf8 *bound )
   {
   // Recognize User-agent, Allow, Disallow, Crawl-delay and
   // Sitemap statements beginning at *start.  Reject blank lines,
   // comments and invalid lines as Invalid.  If a match is found,
   // the text is consumed and *start is updated  to just past the
   // match.


   // YOUR CODE HERE.
   const Utf8 *curr = *start;
   // start from the beginning of the line
   
   //line out of bounds
   if(curr >= bound) {
      // no data to read
      return InvalidStatement;
   }

   //blank line or comment
   if(*curr == ' ' || *curr == '\t' || *curr == '\n' || *curr == '\r' || *curr == '#'){
      // blank, whitespace, or comment line
      return InvalidStatement;
   }

   //try to match all of the keyword types

   char first = curr[0];
   if((first == 'u' || first == 'U') && MatchKeyword(UserAgentString, &curr, bound)){
      // user-agent keyword found
      *start = curr;
      return UserAgentStatement;
   }
   
   if((first == 'a' || first == 'A') && MatchKeyword(AllowString, &curr, bound)){
      // allow keyword found
      *start = curr;
      return AllowStatement;
   }
   
   if((first == 'd' || first == 'D') &&MatchKeyword(DisallowString, &curr, bound)){
      // disallow keyword found
      *start = curr;
      return DisallowStatement;
   }

   if((first == 'c' || first == 'C') &&MatchKeyword(CrawlDelayString, &curr, bound)){
      // crawl-delay keyword found
      *start = curr;
      return CrawlDelayStatement;
   }

   if((first == 's' || first == 'S') &&MatchKeyword(SitemapString, &curr, bound)){
      // sitemap keyword found
      *start = curr;
      return SitemapStatement;
   }

   //if matches none, then invalid
   return InvalidStatement;
   }


Utf8 *GetArgument( const Utf8 **start, const Utf8 *bound )
   {
   // Extract the argument from the input by skipping white space,
   // then taking anything not white space.  Return it as a string
   // on the heap.  Caller responsibility to delete.
   //
   // *start is updated to just past the last character consumed.


   // YOUR CODE HERE.
   const Utf8* s = *start;
   while (s < bound && (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t')){
      // skip leading whitespace
      ++s;
   }

   const Utf8 *end = s;
   while(end < bound && *end != '\r' && *end != '\n' && *end != '\t' && *end != ' '){
      // consume token bytes
      ++end;
   }

   const size_t l = (size_t)(end - s); 

   Utf8 *out = new Utf8[l + 1];
   if ( l ) {
      // copy token into new buffer
      memcpy( out, s, l );
   }
   out[l] = 0;
   // move start past the token
   *start = end;
   return out;
   }


const Utf8 *NextLine( const Utf8 **start, const Utf8 *bound )
   {
   // Consume everything up to the beginning of the next line,
   // updating *start, and return a pointer to it.


   // YOUR CODE HERE.
   const Utf8 *s = *start;
   while(s < bound && *s != '\r' && *s != '\n'){
      // walk to line break
      ++s;
   }

   if( s < bound ){
      if (*s == '\r'){
         ++s;
         if ( s < bound && *s == '\n'){
            // handle windows \r\n
            ++s;
         }
      }else{
         // handle lone \n
         ++s;
      }
   }

   *start = s;
   return *start;
   }



//
// Statement parsing and evaluation
//


StatementType GetStatement( const Utf8 **start, const Utf8 *bound, vector< string > &sitemap )
   {
   // Find the next valid User-agent, Allow, Disallow, or Crawl-delay
   // statement and return the statement type.  Return InvalidStatement if
   // none are found. At entry, assume *start is pointing at the beginning of
   // a line.
   //
   // Skip over any comments, blank lines, or invalid statements as
   // necessary.
   //
   // Sitemap statements are recognized here and the argument URL is added to
   // the sitemap vector, but otherwise skipped because they're global, and
   // don't affect how rules are parsed.


   // YOUR CODE HERE.
   const Utf8 *curr = *start;
   // keep scanning until we see a valid statement
 
   while(curr < bound) {
      const Utf8 *check = curr;
      StatementType t = Identify(&check, bound);
      if( t == InvalidStatement){
         // skip junk lines fast
         curr = NextLine(&curr, bound);
         continue;
      }

      if( t == SitemapStatement){
         // sitemap is global, store and move on
         Utf8 *sm = GetArgument(&check, bound);
         if(sm && *sm){
            sitemap.pushBack((const char *)sm);
         }
         delete [] sm;

         curr = NextLine(&curr, bound);
         continue;
      }

      // found a real statement, return it
      *start = check;
      return t;
   }

   *start = curr;
   return InvalidStatement;
   }


const Utf8 *FindSubstring( const Utf8 *a, const Utf8 *b )
   {
   // Look for an occurrence of string a in string b using case-independent
   // compare.  If a match is found, return a pointer to where in b.  If no
   // match is found, return nullptr.


   // YOUR CODE HERE.
   if(!a || !b){
      // null input, no match
      return nullptr;  
   }

   if ( !*a ){
      // empty needle always matches
      return b;
   }

   for(const Utf8 *ptr = b; *ptr; ++ptr){
      const Utf8 *one = a; 
      const Utf8 *two = ptr;

      while (*one && *two && LowerEqual(*one, *two)){
         // keep going while chars match
         ++one; ++two;
      }

      if (!*one) {
         // matched the entire needle
         return ptr;
      }
   }
   //Did not find a match anywhere.
   return nullptr;
   }


class UserAgent
   {
   public:
      Utf8 *name;
      bool matchAny;

      bool Match( const Utf8 *user )
         {
         // Return true if the name appears anywhere in the string
         // pointed to by user. Use case-independent compare.

         // YOUR CODE HERE.
         if (matchAny){
            // '*' matches all agents
            return true;
         }

         if(!name || !user){
            // missing strings means no match
            return false;
         }

         // case-insensitive substring match
         return FindSubstring(name, user) ? true : false;
         
         }

      UserAgent( const Utf8 **start, const Utf8 *bound )
         {
         // The name is the argument immediately following on
         // the input. Note if it's an * match-any wildcard..

         // YOUR CODE HERE.
         name = GetArgument(start, bound);
         if(name[0] == 0 || (name[0] == '*' && name[1] == 0)){
            // empty or '*' means match-any
            matchAny = true;
         }else{
            matchAny = false;
         }

         }

      ~UserAgent( )
         {
         delete [ ] name;
         }
   };


class Wildcard
   {
   private:
      bool Match( const Utf8 *wildcard, const Utf8 *path )
         {
         // Return true if the path matches the wildcard.
         //
         // Assumes the original URL has been parsed, service, host,
         // port, and any initial slash have been stripped away.
         // path is what remains and has been URL decoded already.
         //
         // If with either wildcard or path is null, return false.

         // YOUR CODE HERE.
         if(!wildcard || !path){
            // nothing to compare
            return false;
         }

         if ( prefixLen > 0 ) {
            if ( strncmp( ( const char * )wildcard,
                  ( const char * )path, prefixLen ) != 0 ) {
               // prefix mismatch, skip expensive match
               return false;
            }
         }

         if ( !hasStar ) {
            if ( endsInDollarSign ) {
               size_t litlen = length - 1;
               // exact match when anchored by $
               return strncmp( ( const char * )wildcard,
                     ( const char * )path, litlen ) == 0 &&
                     path[ litlen ] == 0;
            } else {
               // plain prefix match
               return strncmp( ( const char * )wildcard,
                     ( const char * )path, length ) == 0;
            }
         }

         const Utf8 *w = wildcard;
         const Utf8 *p = path;
         const Utf8 *s = nullptr;
         const Utf8 *r = path;

         while(*p){
            //both literals and match
            if(*w && *w == *p){
               // literal chars line up
               ++w; ++p;
               continue;
            }

            //wildcard has an 'any' char, keep note of for potential other combinations of chars
            if(*w && *w == '*'){
               // remember last star for backtracking
               s = w;
               ++w;
               r = p;
               continue;
            }

            if(!(*w)){
               // wildcard ended, path matched so far
               return true;
            }

            if(s != nullptr){
               // backtrack to last star
               w = s + 1;
               r++;
               p = r;
               continue;
            }

            return false;
         }

         //if path is exhuasted, check the end of the wildcard
         while(*w){
            //trailing stars are ignored
            if(*w == '*'){
               // skip extra stars
               ++w;
            //if the end has a dollar sign and then ends, it's valid
            }else if(*w == '$' && (!*(w + 1))){
               // anchored end-of-string match
               return true;
            //any other chars mean they don't match
            }else{
               return false;
            }
         }


         //if to this point, they match
         return true;
         }


   public:
      Utf8     *wildcard;   // URL-decoded version of the argument
      size_t   length,
               literalBytes;
      size_t   prefixLen;
      bool     hasStar;
      bool     endsInDollarSign;


      Wildcard( const Utf8 **start, const Utf8 *bound )
         {
         // Create a wildcard using the argument immediately
         // following in the input stream.
         //
         // 1. Get the argument.
         // 2. Discard any initial / and create a URL-decoded version.
         // 3. Count how many literal bytes there are.
         // 4. Note the length and whether it ends in $.
         // 5. Discard the original version as no longer needed.

         // YOUR CODE HERE.
         Utf8 *garg = GetArgument(start, bound);
         if ( garg && garg[ 0 ] == '/' ) {
            // drop the leading slash
            Utf8 *dst = garg, *src = garg + 1;
            while ( ( *dst++ = *src++ ) ) {
               ;
            }
         }

         bool needDecode = false;
         hasStar = false;
         prefixLen = 0;
         bool inPrefix = true;
         length = 0;
         literalBytes = 0;
         Utf8 last = 0;

         for ( const Utf8 *t = garg;  *t;  ++t ) {
            if ( *t == '%' ) {
               // percent found, decode later
               needDecode = true;
               break;
            }

            last = *t;
            length++;
            if ( *t != '*' && *t != '$' ) {
               literalBytes++;
               if ( inPrefix ) {
                  prefixLen++;
               }
            } else {
               // hit wildcard or end anchor, stop growing prefix
               if ( *t == '*' ) {
                  hasStar = true;
               }
               inPrefix = false;
            }
         }

         if ( needDecode ) {
            wildcard = UrlDecode( garg );
            delete [ ] garg;

            // recompute on decoded text
            hasStar = false;
            prefixLen = 0;
            inPrefix = true;
            length = 0;
            literalBytes = 0;
            last = 0;

            for ( const Utf8 *t = wildcard;  *t;  ++t ) {
               // same scan as above but on decoded wildcard
               last = *t;
               length++;
               if ( *t != '*' && *t != '$' ) {
                  literalBytes++;
                  if ( inPrefix ) {
                     prefixLen++;
                  }
               } else {
                  if ( *t == '*' ) {
                     hasStar = true;
                  }
                  inPrefix = false;
               }
            }
         } else {
            // no decode needed, keep original buffer
            wildcard = garg;
         }

         endsInDollarSign = ( last == '$' );

         }


      ~Wildcard( )
         {
         delete [ ] wildcard;
         }


      bool Match( const Utf8 *path )
         {
         return Match( wildcard, path );
         }
   };

bool MoreSpecific( const Wildcard *a, const Wildcard *b )
   {
   // Return true if a is more specific than b.

   return a->literalBytes > b->literalBytes ||
         a->literalBytes == b->literalBytes &&
            a->endsInDollarSign && !b->endsInDollarSign;
   }


class Directive
   {
   public:
      StatementType type;
      Wildcard wildcard;

      Directive *Match( const Utf8 *path )
         {
         return wildcard.Match( path ) ? this : nullptr;
         }

      // If more than one directive applies based on the path, choose the one
      // which is
      //
      // 1. More specific, i.e., matches more characters in the URL, which we
      //    will measure as the number of literal (non-wildcard) bytes in the path.
      // 2. Ends in a $.
      // 3. Less restrictive, i.e., choose allow over disallow.
      // 4. Matches a literal User-Agent name, not just *.
      //
      // This means they can be sorted on a list when compiling a rule for a given
      // set of User-Agents; we can take the first one that matches.  But they
      // will still need to be compared if the match different User-agents.

      Directive( const StatementType type, const Utf8 **start, const Utf8 *bound ) :
            type( type ), wildcard( start, bound )
         {
         }

      ~Directive( )
         {
         }
   };


bool TakesPriority( const Directive *a, const Directive *b )
   {
   // Return true if a takes priority over b, meaning a is more specific
   // or less restrictivethan b.

   return  MoreSpecific( &a->wildcard, &b->wildcard ) ||
            a->type == AllowStatement && b->type == DisallowStatement;
   }


class Rule
   {
   public:
      vector< UserAgent * > UserAgents;
      vector< Directive * > Directives;
      int crawlDelay;

      Rule( ) : crawlDelay( 0 )
         {
         }

      ~Rule( )
         {
         // Delete the user agents.
         for ( auto u : UserAgents )
            delete u;

         // Delete the Directives.
         for ( auto d : Directives )
            delete d;
         }


      void AddUserAgent( const Utf8 **start, const Utf8 *bound )
         {
         // A UserAgent statement has been recognized. Create one with
         // the name specified next on the input and add it to the list
         // for this rule.

         UserAgent *u = new UserAgent( start, bound );
         UserAgents.pushBack( u );
         }


      void AddDirective( const StatementType type,
            const Utf8 **start, const Utf8 *bound )
         {
         // An Allow or Disallow directive been found.  Create one with the
         // path specified as the next argument on the input and insertion sort
         // it into the list.

         // YOUR CODE HERE.
         Directive *d = new Directive(type, start, bound);
         Directives.pushBack(d);

         /*
         auto it = Directives.begin();
         
         while(it != Directives.end() && !TakesPriority(d, *it)){
            ++it;
         }  
            
         Directives.insert(d, it);*/

         
         }


      void AddCrawlDelay( const Utf8 **start, const Utf8 *bound )
         {
         // A Crawl-delay statement has been found.  Read the argument
         // and attempt to convert to aninteger with atoi( ).  If it's
         // larger than current delay, use the new value.

         // YOUR CODE HERE.
         Utf8 * arg = GetArgument(start, bound);

         int v = arg ? atoi((const char *)arg) : 0;

         if(v > crawlDelay){
            // keep max delay
            crawlDelay = v;
         }

         delete[] arg;

         }


      Directive *Match( const Utf8 *user, const Utf8 *path, UserAgent **agent )
         {
         // A rule applies if it matches one of the user agents and there is a
         // directive that matches the path.  Since directives are sorted,
         // we can stop at the first match.
         //
         // If a match is found, return a pointer to the directive. Optionally
         // report which UserAgent was matched.

         // YOUR CODE HERE.
         UserAgent * best = nullptr;
         for(auto u : UserAgents){ 
            if(u->Match(user)){
               // prefer specific agent over wildcard
               if(!u->matchAny){
                  best = u;
                  break;
               }

               if(!best){
                  best = u;
               }else if(best->matchAny && !u->matchAny){
                  // upgrade wildcard match to specific
                  best = u;
               }

            }
         }

         if(!best){
            // no agent match for this rule
            return nullptr;
         }

         if(agent){
            *agent = best;
         }

         for(auto d : Directives){
            if(d->Match(path)){
               // directives are sorted, first match wins
               return d;
            }
         }

         return nullptr;
         }

   };


//
// RobotsTxt methods.
//


// Constructor to parse the robots.txt file contents into a
// set of rules and sitemaps.


RobotsTxt::RobotsTxt( const Utf8 *robotsTxt, const size_t length )
   {
   // Read the file one line at a time until the end, parsing
   // it into a vector of rules.

   const Utf8 *p = robotsTxt, *bound = p + length;
   StatementType type;

   // Skip over any byte order mark.

   if ( strncmp( ( const char * )Utf8BOMString,
         ( const char * )p, sizeof( Utf8BOMString) ) == 0 )
      p += sizeof( Utf8BOMString );

   // Skip over everything until a User-agent: is found.

   while ( p < bound &&
         ( type = GetStatement ( &p, bound, sitemap ) ) != UserAgentStatement )
      p = NextLine( &p, bound );

   while ( type == UserAgentStatement )
      {
      // 1. Create a new rule.
      // 2. Collect the user agents.
      // 3. Collect the directives.
      // 4. Add this rule to the list.

   // YOUR CODE HERE.
   Rule *r = new Rule();
      do
         {
         r->AddUserAgent(&p, bound);
         p = NextLine( &p, bound );
         type = GetStatement( &p, bound, sitemap );
         }
      while ( type == UserAgentStatement );


      while ( type == AllowStatement ||
            type == DisallowStatement ||
            type == CrawlDelayStatement )
         {
         if ( type == CrawlDelayStatement )
            r->AddCrawlDelay( &p, bound );
         else
            r->AddDirective( type, &p, bound );

         p = NextLine( &p, bound );
         type = GetStatement( &p, bound, sitemap );
         }

      //sort the directives
      for(size_t i = 1; i < r->Directives.size(); ++i){
         // simple insertion sort by priority
         Directive *s = r->Directives[i];
         size_t j = i;
         while(j > 0 && TakesPriority(s, r->Directives[j - 1])){
            r->Directives[j] = r->Directives[j - 1];
            --j;
         }
         r->Directives[j] = s;
      }

      rules.pushBack(r);
      }
   }


RobotsTxt::~RobotsTxt( )
   {
   // Free up the rules.
   for ( auto r : rules )
      delete r;

   // The sitemap vector will clean up itself.
   }


Directive *RobotsTxt::FindDirective( const Utf8 *user, const Utf8 *path,
      int *crawlDelay, Rule **rule )
   {
   // Check the user and path against the rules in this robots.txt.  Return
   // the highest priority directive if one is found.
   //
   // Optionally report the containing rule and the highest value
   // crawl delay found among all the applicable rules.

   // 1. URL decode the path.
   // 2. Iterate over the rules, looking for one that best applies.

   // YOUR CODE HERE.
   bool needDecode = false;
   for(const Utf8 *t = path; *t; ++t){
      if(*t == '%') {
         // need to decode percent-escapes
         needDecode = true;
         break;
      }
   }

   Utf8 *url_dec = needDecode ? UrlDecode( path ) : nullptr;
   const Utf8 *matchPath = url_dec ? url_dec : path;

   if(rule) *rule = nullptr;
   Directive *bestD = nullptr;
   UserAgent *bestU = nullptr;
   int highCD = 0;
   //iterate over rules
   for(auto r: rules){
      UserAgent *curr = nullptr;
      Directive *d = r->Match(user, matchPath, &curr);
      //if no directive for this rule, skip it
      if(!d){
         // rule doesn't apply
         continue;
      }

      if(r->crawlDelay > highCD){
         // keep max crawl delay
         highCD = r->crawlDelay;
      }

      bool takespri = false;
      //first directive becomes best at that time
      if(!bestD){
         // first match becomes best
         bestD = d;
         bestU = curr;
         if (rule) *rule = r;
      //if this directive takes priority over current best, then set it to best
      }else if((takespri = TakesPriority(d, bestD))){
         // higher priority directive
         bestD = d;
         bestU = curr;
         if (rule) *rule = r;
      //break ties based on more specific user-agents
      }else if(!takespri && !TakesPriority(bestD, d)){
         if(!curr->matchAny && bestU->matchAny){
            // prefer specific agent on tie
            bestD = d;
            bestU = curr;
            if (rule) *rule = r;
         }
      }

   }

   if (crawlDelay){
      *crawlDelay = highCD;  
   }

   if(needDecode){
      delete[] url_dec;
   }

   return bestD;
   }


bool RobotsTxt::UrlAllowed( const Utf8 *user, const Utf8 *url,
      int *crawlDelay )
   {
   // Check a full URL against the rules in this robots.txt.
   // Return true if user is allowed to crawl the URL.
   // Optionally report any crawl delay.

   // Wrapper for PathAllowed that parses the URL first.

   ParsedUrl parsedurl( url );
   return PathAllowed( user, parsedurl.Path, crawlDelay );
   }


bool RobotsTxt::PathAllowed( const Utf8 *user, const Utf8 *path,
      int *crawlDelay )
   {
   // Check the path portion of a URL against the rules in this
   // robots.txt.  Return true if user is allowed to crawl the path.
   // Optionally report any crawl delay. Skip over any initial /
   // in the path.
   //
   // Acts as wrapper for FindDirective to avoid making the definition
   // of a Directive part of the public interface in RobotsTxt.h.

   Directive *d = FindDirective( user, *path == '/' ? path + 1 : path, crawlDelay );
   return !d || d->type == AllowStatement;
   }


vector< string > RobotsTxt::Sitemap( )
   {
   return sitemap;
   }

} // namespace crawler
} // namespace avengine
