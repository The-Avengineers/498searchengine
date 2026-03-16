#pragma once

#include "../avengine_lib/string.h"
#include "../avengine_lib/vector.h"

// url normalizer
// need to add non absolute url support and handling
//  should queries be kept, fragments?

namespace avengine
{
namespace crawler
{

struct URL
{
  string protocol; // http or https
  string host;     // www.google.com
  string port;     // "" for default (removes http + https default ports) OPTIONAL
  string path;     // OPTIONAL ex: /search
                   // string query // skip query for now
};

class URLNormalizer
{
public:
  URL parseURL(const string& url);

  // resolves . and .. segments in a path
  string resolveDotSegments(const string& path);

  // turns the parsed url back into a string, should be in normalized form already
  string URLtoString(const URL& url);

  // Done on the seed list
  // takes in the url, parses it and returns the normalized url
  string normalizeIndividual(const string& url);

  // CALLED BY CRAWLER for links found in html - pass in the base url of the page being crawled and
  // then any other urls found on that page takes in a base url and an extension and normalizes the
  // combined url
  string normalize(const string& base, const string& extension);

private:
  // returns substring from start to end-1
  string substr(const string& str, size_t start, size_t end);

  // lowercases the current string
  string toLower(const string& str);

  // helper functions for port conversion, not used right now -> port is now a string
  // string to int function for the port, returns -1 if not valid
  int stringToInt(const string& str);

  // int to string function for the port
  string intToString(int num);
};

} // namespace crawler
} // namespace avengine
