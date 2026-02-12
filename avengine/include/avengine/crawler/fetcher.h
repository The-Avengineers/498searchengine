#pragma once

#include "../avengine_lib/string.h"
#include "../avengine_lib/expected.h"

namespace avengine {
namespace crawler {

struct fetchResponse {
    string body;
    string headers;
    int statusCode;
};

class fetcher {
public:
    fetcher();
    ~fetcher();

    // Requires: url is a valid HTTP or HTTPS URL string
    // Modifies: nothing persistent (creates/destroys sockets internally)
    // Returns: fetchResponse on success, error on failure
    expected<fetchResponse, error> fetch(const string& url);

private:
    void* sslCtx_;  // SSL_CTX*, opaque to avoid openssl include in header

    // Requires: host, port (as string), path
    // Modifies: nothing persistent
    // Returns: fetchResponse on success via plain TCP 
    expected<fetchResponse, error> fetchHttp(
        const string& host, const string& port, const string& path);

    // Requires: host, port, path
    // Modifies: nothing persistent
    // Returns: fetchResponse on success via SSL/TLS 
    expected<fetchResponse, error> fetchHttps(
        const string& host, const string& port, const string& path);

    // Requires: host and path
    // Modifies: nothing
    // Returns: HTTP GET request string
    string buildRequest(const string& host, const string& path);

    // Requires: raw HTTP response (headers + body)
    // Modifies: nothing
    // Returns: parsed fetchResponse with status code, headers, body
    fetchResponse parseResponse(const string& raw);
};

} 
} 
