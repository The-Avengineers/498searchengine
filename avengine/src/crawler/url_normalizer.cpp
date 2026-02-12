#include "../../include/avengine/crawler/url_normalizer.h"


URL URLNormalizer::parseURL(const string& url) {
    // parse the URL into its components
    //general format is protocol://host:port/path?query
    URL result;

    // strip fragment (#...)
    size_t hashPos = find(url, '#', 0);
    size_t urlSize = (hashPos < url.size()) ? hashPos : url.size();

    // find the protocol
    size_t pos = find(url, ':', 0);
    //checks for ://
    if (pos + 2 < urlSize && url[pos + 1] == '/' && url[pos + 2] == '/') {
        result.protocol = toLower(substr(url, 0, pos));
        pos += 3;
    }else {
        result.protocol = string("http");
        pos = 0;
    }

    //find the host + port combination
    size_t host_start = pos;
    pos = find(url, '/', host_start);
    size_t host_end = pos < urlSize ? pos : urlSize;
    string host_port = toLower(substr(url, host_start, host_end));

    //check for port
    size_t port_pos = find(host_port, ':', 0);
    if (port_pos < host_port.size()) {
        result.host = substr(host_port, 0, port_pos);
        string portStr = substr(host_port, port_pos + 1, host_port.size());
        if((portStr == "80" && result.protocol == "http") ||
        (portStr == "443" && result.protocol == "https")) {
            result.port = string(""); // default port, omit
        } else {
            result.port = portStr;
        }
    } else {
        result.host = host_port;
        result.port = string(""); // default port
    }

    //get the path
    if (pos < urlSize) {
        result.path = substr(url, pos, urlSize);
        if(result.path.size() > 1 && result.path[result.path.size() - 1] == '/') {
            result.path = substr(result.path, 0, result.path.size() - 1); // remove trailing slash
        }
        result.path = resolveDotSegments(result.path);
    } else {
        result.path = string("/");
    }

    return result;
}

// resolves . and .. segments in a path
string URLNormalizer::resolveDotSegments(const string& path) {
    if (path.size() == 0) return string("/");

    // split path into segments by '/'
    vector<string> segments;
    string current;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (current.size() > 0) {
                segments.pushBack(current);
            }
            current = string();
        } else {
            current.pushBack(path[i]);
        }
    }
    if (current.size() > 0) {
        segments.pushBack(current);
    }

    // resolve . and ..
    vector<string> resolved;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (segments[i] == ".") {
            continue;
        } else if (segments[i] == "..") {
            if (resolved.size() > 0) {
                resolved.popBack();
            }
        } else {
            resolved.pushBack(segments[i]);
        }
    }

    // rebuild path
    if (resolved.size() == 0) return string("/");
    string result;
    for (size_t i = 0; i < resolved.size(); ++i) {
        result += "/";
        result += resolved[i];
    }
    return result;
}

//turns the parsed url back into a string, should be in normalized form already
string URLNormalizer::URLtoString(const URL& url) {
    string result = url.protocol + "://" + url.host;
    if (url.port.size() > 0) {
        result += ":" + url.port;
    }
    result += url.path;
    return result;
}

//takes in the url, parses it and returns the normalized url
string URLNormalizer::normalize(const string& url) {
    URL parsedURL = parseURL(url);
    return URLtoString(parsedURL);
}

//find char in string, returns index or str.size()
size_t URLNormalizer::find(const string& str, char c, size_t start_pos) {
    for (size_t i = start_pos; i < str.size(); ++i) {
        if (str[i] == c) {
            return i;
        }
    }
    return str.size();
}

//returns substring from start to end-1
string URLNormalizer::substr(const string& str, size_t start, size_t end) {
    string result;
    for (size_t i = start; i < end && i < str.size(); ++i) {
        result.pushBack(str[i]);
    }
    return result;
}

//lowercases the current string
string URLNormalizer::toLower(const string& str) {
    string result;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        }
        result.pushBack(c);
    }
    return result;
}

//helper functions for port conversion, not used right now -> port is now a string
//string to int function for the port, returns -1 if not valid
int URLNormalizer::stringToInt(const string& str) {
    int result = 0;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            result = result * 10 + (c - '0');
        } else {
            // handle non-digit characters, could throw an error or return -1
            return -1;
        }
    }
    return result;
}

// int to string function for the port
string URLNormalizer::intToString(int num) {
    if (num == 0) return "0";
    string result;
    while (num > 0) {
        char c = '0' + (num % 10);
        result = c + result;
        num /= 10;
    }
    return result;
}
