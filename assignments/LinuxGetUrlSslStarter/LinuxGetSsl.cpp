#include <cassert>
#include <iostream>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

class ParsedUrl {
public:
  const char *CompleteUrl;
  char *Service, *Host, *Port, *Path;

  ParsedUrl(const char *url) {
    // Assumes url points to static text but
    // does not check.

    CompleteUrl = url;

    pathBuffer = new char[strlen(url) + 1];
    const char *f;
    char *t;
    for (t = pathBuffer, f = url; (*t++ = *f++);)
      ;

    Service = pathBuffer;

    const char Colon = ':', Slash = '/';
    char *p;
    for (p = pathBuffer; *p && *p != Colon; p++)
      ;

    if (*p) {
      // Mark the end of the Service.
      *p++ = 0;

      if (*p == Slash)
        p++;
      if (*p == Slash)
        p++;

      Host = p;

      for (; *p && *p != Slash && *p != Colon; p++)
        ;

      if (*p == Colon) {
        // Port specified.  Skip over the colon and
        // the port number.
        *p++ = 0;
        Port = +p;
        for (; *p && *p != Slash; p++)
          ;
      } else
        Port = p;

      if (*p)
        // Mark the end of the Host and Port.
        *p++ = 0;

      // Whatever remains is the Path.
      Path = p;
    } else
      Host = Path = p;
  }

  ~ParsedUrl() { delete[] pathBuffer; }

private:
  char *pathBuffer;
};

#define USER_AGENT "LinuxGetUrl/2.0 chasehad@umich.edu (Linux)"

char *generate_header(ParsedUrl const &url) {
  const char *p1 = "GET /";
  const char *p2 = " HTTP/1.1\r\nHost: ";
  const char *p3 =
      "\r\nUser-Agent: " USER_AGENT "\r\nAccept: */*\r\nAccept-Encoding: "
      "identity\r\nConnection: close\r\n\r\n";

  char *header = new char[strlen(p1) + strlen(p2) + strlen(p3) +
                          strlen(url.Host) + strlen(url.Path) + 1]();

  memcpy(header, p1, strlen(p1));
  memcpy(header + strlen(p1), url.Path, strlen(url.Path));
  memcpy(header + strlen(p1) + strlen(url.Path), p2, strlen(p2));
  memcpy(header + strlen(p1) + strlen(url.Path) + strlen(p2), url.Host,
         strlen(url.Host));
  memcpy(header + strlen(p1) + strlen(url.Path) + strlen(p2) + strlen(url.Host),
         p3, strlen(p3));

  return header;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " url" << std::endl;
    return 1;
  }

  SSL_library_init();

  SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

  // Parse the URL
  ParsedUrl url(argv[1]);

  struct addrinfo *address, hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(url.Host, url.Service, &hints, &address) != 0) {
    std::cerr << "Failed to find address info" << std::endl;
    return 2;
  }

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd == -1) {
    std::cerr << "Failed to find address info (errno = " << errno << ")"
              << std::endl;
    return 3;
  }

  int con_res = connect(fd, address->ai_addr, address->ai_addrlen);

  if (con_res == -1) {
    std::cerr << "Failed to connect (errno = " << errno << ")" << std::endl;
    return 4;
  }

  SSL *ssl = SSL_new(ctx);
  SSL_set_tlsext_host_name(ssl, url.Host);

  assert(SSL_set_fd(ssl, fd) > 0);

  assert(SSL_connect(ssl) > 0);

  char const *header = generate_header(url);

  ssize_t sent = SSL_write(ssl, header, strlen(header));

  if (sent == -1) {
    std::cerr << "Failed to send to socket (errno = " << errno << ")"
              << std::endl;
    return 5;
  }

  char buffer[4096];
  int bytes;
  bool seen_header = false;
  while ((bytes = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
    if (!seen_header) {
      for (size_t i = 3; i < bytes; i++) {
        if (buffer[i - 3] == '\r' && buffer[i - 2] == '\n' &&
            buffer[i - 1] == '\r' && buffer[i] == '\n') {
          seen_header = true;
          assert(write(1, &buffer[i + 1], bytes - i - 1) >= 0);
          break;
        }
      }
    } else
      assert(write(1, buffer, bytes) >= 0);
  }

  // sendfile(1, fd);

  close(fd);
  freeaddrinfo(address);
}
