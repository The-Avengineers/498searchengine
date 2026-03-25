// RobotsTxt.h
//
// Nicole Hamilton, nham@umich.edu

#pragma once
#include "../avengine_lib/string.h"
#include "../avengine_lib/vector.h"
#include "../Utf8.h"


// This file describes a simple RobotsTxt class that can compile a
// robots.txt file and can then be used to determine whether access
// is allowed or disallowed for a given user agent and URL.

// We will take direction on the file format definition from Google at
// https://developers.google.com/search/docs/crawling-indexing/robots/robots_txt
// and from https://www.robotstxt.org.

// A robots.txt file is organized as a series of rules.  Each rule
// starts with one or more User-agent statements identifying the
// robots the rule applies to. That's followed by any number of
// Allow, Disallow or Crawl-delay statements.  Allow and Disallow
// specify paths that allowed or disallowed.  Crawl-delay specifies
// how long to wait before recrawling the site.
//
// The rules are not in any order.  All of them must be checked.  If
// more than one apply, choose the one that is most specific, ends in
// $ or less restrictive.
//
// The file is expected to be encoded as Utf-8. Any Byte Order Mark
// (BOM) at the beginning is ignored.
//
// A robots.txt file applies only to paths within the protocol, host,
// and port where it is posted.

// It's line-oriented.  Comments (beginning with #) and New User-agent,
// Allow, and Disallow directives must start at the beginning of a line.
// Line ends can be \r\n, \r, or \n.

// Comments and blank or invalid lines are ignored.

// The keywords, "User-agent", "Allow", "Disallow", "Sitemap", and
// robot names are case-insensitive.  But paths ARE case-sensitive.

// One or more User-agent statements with no directives in between are
// taken as group. Any Allow or Disallow directives that follow apply
// to the group.  A new User-agent statement begins a new group.


//
// Robot.txt syntax
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
// User-agent:
//


// A User-agent: statement specifies a single robot name (or name)
// or *, meaning all robots.  Multiple User-agent statements one after
// another are taken as a group.
//
// If the name is missing, behavior is undefined, but most crawlers
// treat that as same as * and we will do the same.
//
// The User-agent statements are followed by any number of Allow:
// or Disallow: directives meant to apply to that group of robots.
//
// A new User-agent statement begins a new group of directives.

// In deciding whether access is allowed to a robot, we will follow
// the never-adopted but largely followed 1997 draft specification at
// https://www.robotstxt.org/norobots-rfc.txt:
//
// "The robot must obey the first record in /robots.txt that contains
// a User-Agent line whose value contains the name name of the robot
// as a substring.(*)  The name comparisons are case-insensitive. If no
// such record exists, it should obey the first record with a User-agent
// line with a "*" value, if present. If no record satisfied either
// condition, or no records are present at all, access is unlimited."

// (*) If name specified in the User-Agent matches a part of your
//     crawler's name, this applies to you. It is not a wildcard
//     match, it's just a literal, but case-insensitive substring match.


//
// Allow: and Disallow: directives
//


// Directives not preceded by a User-agent are ignored.
//
// Allow and Disallow directives specify a path, which must start with /
// or * and is compared against the path portion of the URL being considered.
// If that first part of the URL matches the path, the rest of the URL
// doesn't matter.
//
// Paths that do not start with / or * are technically illegal but treated by
// most bots as if they started with /.  If it does begins with /, skip that
// initial character when writing out the wildcard.
//
// The path may contain * and $ wildcard characters.  * matches any
// number of arbitrary character as necessary to make the rest of the
// pattern match.  $ matches the end of the URL.
//
// Either the path in the directive or the path being compared may contain
// %xx directives.  This is called URL encoding, used to specify a character
// by its hex value.
//
//
// Google advises:
//
//    The matching process compares every octet in the path portion of
//    the URL and the path from the record. If a %xx encoded octet is
//    encountered it is unencoded prior to comparison, unless it is the
//    "/" character, which has special meaning in a path. The match
//    evaluates positively if and only if the end of the path from the
//    record is reached before a difference in octets is encountered.

//    This table illustrates some examples:
//
//      Record Path        URL path         Matches
//      /tmp               /tmp               yes
//      /tmp               /tmp.html          yes
//      /tmp               /tmp/a.html        yes
//      /tmp/              /tmp               no
//      /tmp/              /tmp/              yes
//      /tmp/              /tmp/a.html        yes
//
//      /a%3cd.html        /a%3cd.html        yes
//      /a%3Cd.html        /a%3cd.html        yes
//      /a%3cd.html        /a%3Cd.html        yes
//      /a%3Cd.html        /a%3Cd.html        yes
//
//      /a%2fb.html        /a%2fb.html        yes
//      /a%2fb.html        /a/b.html          no
//      /a/b.html          /a%2fb.html        no
//      /a/b.html          /a/b.html          yes
//
//      /%7ejoe/index.html /~joe/index.html   yes
//      /~joe/index.html   /%7Ejoe/index.html yes
//
//
// What this means is that %xx URL encodings (unless it's %2f = '/')
// are processed in both the directive and path before being compared.
// If it's a %2f in either, it's literal.


//
// If more than one directive applies
//


// If more than one directive applies based on the path, choose the one
// which is:
//
// 1. More specific, i.e., matches more characters in the URL, which we
//    will measure as the number of literal (non-wildcard) bytes in the path.
// 2. Ends in a $.
// 3. Less restrictive, i.e., choose allow over disallow.
// 4. Matches a literal User-Agent name, not just *.
//


//
// Crawl-delay:
//

// Crawl-delay: takes an unsigned integer argument specifying a delay
// in seconds before recrawlng.  It's ambiguous whether that's the delay
// between GET requests or the time between when the first GET finishes
// and the next one starts.
//
// Google no longer recognizes Crawl-delay.  Bingbot and YandexBot do
// support it but interpret it differently.
//
// We'll recognize Crawl-delay if present. If it's not present, we'll
// assume a default = 0. If multiple Crawl-delays are given, we'll use
// the largest.


//
// Sitemap:
//


// Sitemap statements direct robots to an XML file intended as a roadmap
// for search engines.  The argument is supposed to be a full URL.  There
// can be multiple sitemap statements for different parts of a large website.
//
// Sitemap statements are global.  They apply to every robot and almost like
// a comment, it doesn't matter where they apper.



namespace avengine
{
namespace crawler
{

class Rule;
class Directive;

class RobotsTxt
{
private:
  vector<class Rule*> rules;
  vector<string> sitemap;

  Directive* FindDirective(const Utf8* user, const Utf8* url, int* crawlDelay = nullptr,
                           class Rule** rule = nullptr);

public:
  // Constructor to parse the robots.txt file contents into a
  // set of rules.
  RobotsTxt(const Utf8* robotsTxt, const size_t length);

  // Destructor
  ~RobotsTxt();

  // Check a full URL against the rules in this robots.txt.
  // Return true if access is allowed.  Optionally report
  // any crawl delay.
  bool UrlAllowed(const Utf8* user, const Utf8* url, int* crawlDelay = nullptr);

  // Check the path portion of a URL against the rules in this
  // robots.txt.  Return true if user is allowed to crawl the path.
  // Optionally report any crawl delay.
  bool PathAllowed(const Utf8* user, const Utf8* path, int* crawlDelay = nullptr);

  // Get the list of paths specified in any Sitemap statements.
  vector<string> Sitemap();
};

} // namespace crawler
} // namespace avengine



