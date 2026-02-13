#include "../../include/avengine/crawler/url_normalizer.h"

URL URLNormalizer::parseURL(const string& url)
{
  // parse the URL into its components
  // general format is protocol://host:port/path?query
  URL result;

  // strip fragment (#...)
  size_t hashPos = url.find("#");
  size_t urlSize = (hashPos != string::npos) ? hashPos : url.size();

  // find the protocol
  size_t pos = url.find("://");
  if (pos != string::npos && pos < urlSize)
  {
    result.protocol = toLower(substr(url, 0, pos));
    pos += 3; // skip past ://
  }
  else
  {
    result.protocol = string("http");
    pos = 0;
  }

  // find the host + port combination
  size_t host_start = pos;
  pos = url.find("/", host_start);
  size_t host_end = (pos != string::npos && pos < urlSize) ? pos : urlSize;
  string host_port = toLower(substr(url, host_start, host_end));

  // check for port
  size_t port_pos = host_port.find(":");
  if (port_pos != string::npos)
  {
    result.host = substr(host_port, 0, port_pos);
    string portStr = substr(host_port, port_pos + 1, host_port.size());
    if ((portStr == "80" && result.protocol == "http") ||
        (portStr == "443" && result.protocol == "https"))
    {
      result.port = string(""); // default port, omit
    }
    else
    {
      result.port = portStr;
    }
  }
  else
  {
    result.host = host_port;
    result.port = string(""); // default port
  }

  // get the path
  if (pos < urlSize)
  {
    result.path = substr(url, pos, urlSize);
    if (result.path.size() > 1 && result.path[result.path.size() - 1] == '/')
    {
      result.path = substr(result.path, 0, result.path.size() - 1); // remove trailing slash
    }
    result.path = resolveDotSegments(result.path);
  }
  else
  {
    result.path = string("/");
  }

  return result;
}

// resolves . and .. segments in a path
string URLNormalizer::resolveDotSegments(const string& path)
{
  if (path.size() == 0)
    return string("/");

  // split path into segments by '/'
  vector<string> segments;
  string current;
  for (size_t i = 0; i < path.size(); ++i)
  {
    if (path[i] == '/')
    {
      if (current.size() > 0)
      {
        segments.pushBack(current);
      }
      current = string();
    }
    else
    {
      current.pushBack(path[i]);
    }
  }
  if (current.size() > 0)
  {
    segments.pushBack(current);
  }

  // resolve . and ..
  vector<string> resolved;
  for (size_t i = 0; i < segments.size(); ++i)
  {
    if (segments[i] == ".")
    {
      continue;
    }
    else if (segments[i] == "..")
    {
      if (resolved.size() > 0)
      {
        resolved.popBack();
      }
    }
    else
    {
      resolved.pushBack(segments[i]);
    }
  }

  // rebuild path
  if (resolved.size() == 0)
    return string("/");
  string result;
  for (size_t i = 0; i < resolved.size(); ++i)
  {
    result += "/";
    result += resolved[i];
  }
  return result;
}

// turns the parsed url back into a string, should be in normalized form already
string URLNormalizer::URLtoString(const URL& url)
{
  string result = url.protocol + "://" + url.host;
  if (url.port.size() > 0)
  {
    result += ":" + url.port;
  }
  result += url.path;
  return result;
}

// takes in the url, parses it and returns the normalized url
string URLNormalizer::normalizeIndividual(const string& url)
{
  URL parsedURL = parseURL(url);
  return URLtoString(parsedURL);
}

string URLNormalizer::normalize(const string& base, const string& extension)
{
  // If extension contains "://", it's an absolute URL
  if (extension.find("://") != string::npos)
  {
    return normalizeIndividual(extension);
  }

  URL baseURL = parseURL(base);

  // If extension starts with "//", it's protocol-relative - use the protocl of the base url
  if (extension.size() >= 2 && extension[0] == '/' && extension[1] == '/')
  {
    return normalizeIndividual(baseURL.protocol + ":" + extension);
  }

  // If extension starts with "/", it's an absolute path - use the root of base url
  if (extension.size() > 0 && extension[0] == '/')
  {
    baseURL.path = resolveDotSegments(extension);
    return URLtoString(baseURL);
  }

  // Otherwise it's a relative path — merge with base path
  size_t lastSlash = 0;
  for (size_t i = 0; i < baseURL.path.size(); ++i)
  {
    if (baseURL.path[i] == '/')
      lastSlash = i;
  }
  baseURL.path = resolveDotSegments(substr(baseURL.path, 0, lastSlash + 1) + extension);
  return URLtoString(baseURL);
}

// returns substring from start to end-1
string URLNormalizer::substr(const string& str, size_t start, size_t end)
{
  string result;
  for (size_t i = start; i < end && i < str.size(); ++i)
  {
    result.pushBack(str[i]);
  }
  return result;
}

// lowercases the current string
string URLNormalizer::toLower(const string& str)
{
  string result;
  for (size_t i = 0; i < str.size(); ++i)
  {
    char c = str[i];
    if (c >= 'A' && c <= 'Z')
    {
      c = c - 'A' + 'a';
    }
    result.pushBack(c);
  }
  return result;
}

// helper functions for port conversion, not used right now -> port is now a string
// string to int function for the port, returns -1 if not valid
int URLNormalizer::stringToInt(const string& str)
{
  int result = 0;
  for (size_t i = 0; i < str.size(); ++i)
  {
    char c = str[i];
    if (c >= '0' && c <= '9')
    {
      result = result * 10 + (c - '0');
    }
    else
    {
      // handle non-digit characters, could throw an error or return -1
      return -1;
    }
  }
  return result;
}

// int to string function for the port
string URLNormalizer::intToString(int num)
{
  if (num == 0)
    return "0";
  string result;
  while (num > 0)
  {
    char c = '0' + (num % 10);
    result = c + result;
    num /= 10;
  }
  return result;
}
