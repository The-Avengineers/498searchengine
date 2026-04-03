#include "../../include/avengine/crawler/url_normalizer.h"

namespace avengine
{
namespace crawler
{
namespace
{

bool endsWith(const string& value, const char* suffix)
{
  size_t suffixLen = 0;
  while (suffix[suffixLen] != '\0')
    ++suffixLen;

  if (value.size() < suffixLen)
    return false;

  size_t offset = value.size() - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i)
  {
    if (value[offset + i] != suffix[i])
      return false;
  }
  return true;
}

void canonicalizeRedditHost(string& host)
{
  if (host == "reddit.com" || endsWith(host, ".reddit.com"))
  {
    host = "old.reddit.com";
  }
}

} // namespace

URL URLNormalizer::parseURL(const string& url)
{
  // parse the URL into its components
  // general format is protocol://host:port/path?query
  URL result;

  // make a copy to use find() (non-const)
  string localUrl = url;

  // ignore fragments denoted by #
  size_t hashPos = localUrl.find("#");
  size_t urlSize = (hashPos != string::npos) ? hashPos : localUrl.size();

  // find the protocol
  size_t pos = localUrl.find("://");
  if (pos != string::npos && pos < urlSize)
  {
    result.protocol = toLower(substr(localUrl, 0, pos));
    pos += 3; // skip past ://
  }
  else
  {
    result.protocol = string("http");
    pos = 0;
  }

  // Host/port runs from after scheme up to first '/' (or end of URL).
  size_t host_start = pos;
  pos = localUrl.find("/", host_start);
  size_t host_end = (pos != string::npos && pos < urlSize) ? pos : urlSize;
  string host_port = toLower(substr(localUrl, host_start, host_end));

  // Split host and port (if present).
  size_t port_pos = host_port.find(":");
  if (port_pos != string::npos)
  {
    result.host = substr(host_port, 0, port_pos);
    string portStr = substr(host_port, port_pos + 1, host_port.size());
    if ((portStr == "80" && result.protocol == "http") ||
        (portStr == "443" && result.protocol == "https"))
    {
      result.port = string(""); // default port for that protocol
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
  canonicalizeRedditHost(result.host);

  // Path includes query strings
  if (pos < urlSize)
  {
    result.path = substr(localUrl, pos, urlSize);
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

  // Split on '/' and drop empty pieces to collapse repeated slashes.
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

  // Resolve navigation markers in order.
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

  // Rebuild as absolute path.
  if (resolved.size() == 0)
    return string("/");
  string result;
  for (size_t i = 0; i < resolved.size(); ++i)
  {
    result += "/";
    result += resolved[i];
  }
  // Preserve a trailing slash if the original path had one.
  if (path[path.size() - 1] == '/')
  {
    result += "/";
  }
  return result;
}

// turns the parsed url back into a string, should be in normalized form already
string URLNormalizer::URLtoString(const URL& url)
{
  string result = url.protocol;
  result += "://";
  result += url.host;
  if (url.port.size() > 0)
  {
    result += ":";
    result += url.port;
  }
  result += url.path;
  return result;
}

// takes in the url, parses it and returns the normalized url
string URLNormalizer::normalizeIndividual(const string& url)
{
  string localUrl = url;

  // Repair malformed absolute URLs with extra slashes after scheme:
  // https:///www.google.com -> https://www.google.com
  size_t schemePos = localUrl.find("://");
  if (schemePos != string::npos)
  {
    size_t hostStart = schemePos + 3;
    size_t firstNonSlash = hostStart;
    while (firstNonSlash < localUrl.size() && localUrl[firstNonSlash] == '/')
    {
      ++firstNonSlash;
    }

    if (firstNonSlash > hostStart)
    {
      string repaired = substr(localUrl, 0, hostStart);
      repaired += substr(localUrl, firstNonSlash, localUrl.size());
      localUrl = repaired;
    }
  }

  URL parsedURL = parseURL(localUrl);
  if (parsedURL.host.size() == 0)
  {
    return string();
  }
  return URLtoString(parsedURL);
}

string URLNormalizer::normalize(const string& base, const string& extension)
{
  // make a non-const copy so we can call find() (which is not const)
  string localExt = extension;

  // Fully-qualified link: normalize as-is.
  if (localExt.find("://") != string::npos)
  {
    return normalizeIndividual(localExt);
  }

  URL baseURL = parseURL(base);
  if (baseURL.host.size() == 0)
  {
    return string();
  }

  // replace host + path
  if (localExt.size() >= 2 && localExt[0] == '/' && localExt[1] == '/')
  {
    size_t hostStart = 2;
    while (hostStart < localExt.size() && localExt[hostStart] == '/')
    {
      ++hostStart;
    }

    string tmp = baseURL.protocol;
    tmp += "://";
    tmp += substr(localExt, hostStart, localExt.size());
    return normalizeIndividual(tmp);
  }

  // Absolute path on same host.
  if (localExt.size() > 0 && localExt[0] == '/')
  {
    baseURL.path = resolveDotSegments(localExt);
    return URLtoString(baseURL);
  }

  // Relative link: replace everything after the last slash in base path.
  size_t lastSlash = 0;
  for (size_t i = 0; i < baseURL.path.size(); ++i)
  {
    if (baseURL.path[i] == '/')
      lastSlash = i;
  }
  string combined = substr(baseURL.path, 0, lastSlash + 1);
  combined += localExt;
  baseURL.path = resolveDotSegments(combined);
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
    return string("0");
  char buf[12];
  int len = 0;
  while (num > 0)
  {
    buf[len++] = '0' + (num % 10);
    num /= 10;
  }
  string result;
  for (int i = len - 1; i >= 0; --i)
    result.pushBack(buf[i]);
  return result;
}

} // namespace crawler
} // namespace avengine
