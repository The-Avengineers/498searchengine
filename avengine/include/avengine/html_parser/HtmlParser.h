// HtmlParser.h
// Nicole Hamilton, nham@umich.edu

#pragma once

#include "HtmlTags.h"
#include "../avengine_lib/string.h"
#include "../avengine_lib/vector.h"

namespace avengine
{

// This is a simple HTML parser class.  Given a text buffer containing
// a presumed HTML page, the constructor will parse the text to create
// lists of words, title words and outgoing links found on the page.  It
// does not attempt to parse the entire the document structure.
//
// The strategy is to word-break at whitespace and HTML tags and discard
// most HTML tags.  Three tags require discarding everything between
// the opening and closing tag. Five tags require special processing.
//
// We will use the list of possible HTML element names found at
// https://developer.mozilla.org/en-US/docs/Web/HTML/Element +
// !-- (comment), !DOCTYPE and svg, stored as a table in HtmlTags.h.

// Here are the rules for recognizing HTML tags.
//
// 1. An HTML tag starts with either < if it's an opening tag or </ if it's
//    a closing token.  If it starts with < and ends with /> it is both.
//
// 2. The name of the tag must follow the < or </ immediately.  There can't
//    be any whitespace.
//
// 3. The name is terminated by whitespace, > or / and is case-insensitive.
//    The exception is <!--, which starts a comment and is not required
//    to be terminated.
//
// 4. If the name is terminated by whitepace, arbitrary text representing
//    various arguments may follow, terminated by a > or />.
//
// 5. If the name isn't on the list we recognize, we assume it's just
//    ordinary text.
//
// 6. Every token is taken as a word-break.
//
// 7. Most opening or closing tokens can simply be discarded.
//
// 8. <script>, <style>, and <svg> require discarding everything between the
//    opening and closing tag.  Unmatched closing tags are discarded.
//
// 9. <!--, <title>, <a>, <base> and <embed> require special processing.
//
//      <!-- is the beginng of a comment.  Everything up to the ending -->
//          is discarded.
//
//      <title> should cause all the words between the opening and closing
//          tags to be added to the titleWords vector rather than the default
//          words vector.  A closing </title> without an opening <title> is discarded.
//
//      <a> is expected to contain an href="...url..."> argument with the
//          URL inside the double quotes that should be added to the list
//          of links.  All the words in between the opening and closing tags
//          should be collected as anchor text associated with the link
//          in addition to being added to the words or titleWords vector,
//          as appropriate.  A closing </a> without an opening <a> is
//          discarded.
//
//     <base> may contain an href="...url..." parameter.  If present, it should
//          be captured as the base variable.  Only the first is recognized; any
//          others are discarded.
//
//     <embed> may contain a src="...url..." parameter.  If present, it should be
//          added to the links with no anchor text.

class Link
{
public:
  string URL;
  vector<string> anchorText;
  Link(const string& URL) : URL(URL) {}
};
class HtmlParser
{
public:
  vector<string> words, titleWords;
  vector<Link> links;
  string base;

private:
  // curr ptr
  const char* curr;
  // end ptr
  const char* end;
  bool curr_in_title;
  bool curr_in_anchor;
  bool seen_base;
  enum class tag_result
  {
    not_tag,
    no_anchor_change,
    anchor_open,
    anchor_closed
  };
  static inline char lower_ascii(unsigned char c)
  {
    if (c >= 'A' && c <= 'Z')
      return static_cast<char>(c + 32);
    return static_cast<char>(c);
  }
  // check for spaces, tabs, newlines, returns
  static inline bool is_whitespace(char c)
  {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  }
  // build a string from a pointer range
  static string makeString(const char* start_, const char* end_)
  {
    string s;
    for (const char* p = start_; p < end_; ++p)
    {
      s.pushBack(*p);
    }
    return s;
  }
  // add to correct data structures
  void add_to_words(const char* start_, const char* end_, bool in_title, int anchor_link_idx)
  {

    string word = makeString(start_, end_);
    if (in_title)
    {
      titleWords.pushBack(word);
    }
    else
    {
      words.pushBack(word);
    }
    if (anchor_link_idx >= 0 && static_cast<size_t>(anchor_link_idx) < links.size())
    {
      links[anchor_link_idx].anchorText.pushBack(word);
    }
  }
  // move curr past the section
  void discard_section(const char* lowercaseName, size_t len)
  {
    while (curr < end)
    {
      if (*curr == '<' && curr + 1 < end && *(curr + 1) == '/')
      {
        const char* tag_start = curr + 2;
        const char* p = tag_start;
        while (p < end && *p != '>' && *p != '/' && !is_whitespace(*p))
        {
          p++;
        }

        size_t new_len = p - tag_start;
        if (new_len == len)
        {
          bool match = true;
          for (size_t i = 0; i < len && match; i++)
          {
            if (lower_ascii((unsigned char)tag_start[i]) != lowercaseName[i])
            {
              match = false;
            }
          }

          if (match)
          {
            while (curr < end && *curr != '>')
            {
              curr++;
            }
            if (curr < end)
            {
              curr++;
            }
            return;
          }
        }
      }
      curr++;
    }
  }
  // get src
  string get_src(const char* start, const char* tag_end)
  {
    const char* s = start;

    while (s + 3 <= tag_end)
    {
      if ((s == start || is_whitespace(*(s - 1))) && lower_ascii((unsigned char)*s) == 's' &&
          lower_ascii((unsigned char)s[1]) == 'r' && lower_ascii((unsigned char)s[2]) == 'c')
      {
        s += 3;

        while (s < tag_end && is_whitespace(*s))
        {
          s++;
        }

        if (s < tag_end && *s == '=')
        {
          s++;

          while (s < tag_end && is_whitespace(*s))
          {
            s++;
          }

          if (s < tag_end && *s == '"')
          {
            s++;
            const char* valueStart = s;
            while (s < tag_end && *s != '"')
            {
              s++;
            }
            return makeString(valueStart, s);
          }
        }
      }
      s++;
    }
    return string();
  }
  // get href
  string get_href(const char* start, const char* tag_end)
  {
    const char* s = start;

    while (s + 4 <= tag_end)
    {
      if ((s == start || is_whitespace(*(s - 1))) && lower_ascii((unsigned char)*s) == 'h' &&
          lower_ascii((unsigned char)s[1]) == 'r' && lower_ascii((unsigned char)s[2]) == 'e' &&
          lower_ascii((unsigned char)s[3]) == 'f')
      {
        s += 4;
        while (s < tag_end && is_whitespace(*s))
        {
          s++;
        }
        if (s < tag_end && *s == '=')
        {
          s++;

          while (s < tag_end && is_whitespace(*s))
          {
            s++;
          }

          if (s < tag_end && *s == '"')
          {
            s++;
            const char* value_start = s;
            while (s < tag_end && *s != '"')
            {
              s++;
            }
            return makeString(value_start, s);
          }
        }
      }
      s++;
    }
    return string();
  }
  // proccess the tag
  tag_result process_tag()
  {
    tag_result res = tag_result::no_anchor_change;
    const char* start_of_tag = curr;
    curr++;

    if (curr >= end)
    {
      curr = start_of_tag;
      return tag_result::not_tag;
    }

    bool is_closing = false;
    if (*curr == '/')
    {
      is_closing = true;
      curr++;
      if (curr >= end)
      {
        curr = start_of_tag;
        return tag_result::not_tag;
      }
    }

    const char* start_of_name = curr;

    if (!is_closing && curr + 2 < end && *curr == '!' && *(curr + 1) == '-' && *(curr + 2) == '-')
    {
      curr += 3; // skip !-- bc comment
      while (curr < end)
      {
        if (*curr == '-' && curr + 2 < end && *(curr + 1) == '-' && *(curr + 2) == '>')
        {
          curr += 3;
          break;
        }
        curr++;
      }
      return tag_result::no_anchor_change;
    }

    // read tag
    while (curr < end && *curr != '>' && *curr != '/' && !is_whitespace(*curr))
    {
      curr++;
    }

    const char* name_end = curr;

    // empty tag
    if (start_of_name == name_end)
    {
      curr = start_of_tag;
      return tag_result::not_tag;
    }

    size_t name_len = name_end - start_of_name;
    char lowercase_name[32];
    if (name_len >= sizeof(lowercase_name))
    {
      // wayy to long
      curr = start_of_tag;
      return tag_result::not_tag;
    }

    for (size_t i = 0; i < name_len; i++)
    {
      lowercase_name[i] = lower_ascii((unsigned char)start_of_name[i]);
    }
    lowercase_name[name_len] = '\0';

    // get action
    DesiredAction action = LookupPossibleTag(lowercase_name, lowercase_name + name_len);

    if (action == DesiredAction::OrdinaryText)
    {
      curr = start_of_tag;
      return tag_result::not_tag;
    }

    const char* attr_start = curr;
    bool in_quotes = false;
    char quote_ch = '\0';
    while (curr < end)
    {
      char ch = *curr;
      if (!in_quotes && (ch == '"' || ch == '\''))
      {
        // Ignore stray quote immediately before tag end like class="..."" >
        if ((curr + 1 < end && curr[1] == '>') ||
            (curr + 2 < end && curr[1] == '/' && curr[2] == '>'))
        {
          // do nothing
        }
        else
        {
          in_quotes = true;
          quote_ch = ch;
        }
      }
      else if (in_quotes && ch == quote_ch)
      {
        in_quotes = false;
      }
      else if (!in_quotes && ch == '>')
      {
        break;
      }
      curr++;
    }
    const char* tag_end = curr;
    bool self_closing = false;
    {
      const char* t = tag_end;
      while (t > start_of_tag && is_whitespace(*(t - 1)))
      {
        t--;
      }
      if (t > start_of_tag && *(t - 1) == '/')
      {
        self_closing = true;
      }
    }
    if (curr < end)
    {
      curr++;
    }

    // Handle the tag based on its action
    switch (action)
    {
    case DesiredAction::Discard:
      break;

    case DesiredAction::DiscardSection:
      // discard everything
      if (!is_closing)
      {
        discard_section(lowercase_name, name_len);
      }
      break;

    case DesiredAction::Title:
      if (is_closing || self_closing)
      {
        curr_in_title = false;
      }
      else
      {
        curr_in_title = true;
      }
      break;

    case DesiredAction::Anchor:
      if (is_closing)
      {
        curr_in_anchor = false;
        res = tag_result::anchor_closed;
      }
      else
      {
        string href = get_href(attr_start, tag_end);
        if (href.size() > 0)
        {
          links.pushBack(Link(href));
          res = tag_result::anchor_open;
        }
        curr_in_anchor = (!self_closing && href.size() > 0);
      }
      break;

    case DesiredAction::Base:
      if (!is_closing && !seen_base)
      {
        string href = get_href(attr_start, tag_end);
        if (href.size() > 0)
        {
          base = href;
          seen_base = true;
        }
      }
      break;

    case DesiredAction::Embed:
      if (!is_closing)
      {
        string src = get_src(attr_start, tag_end);
        if (src.size() > 0)
          links.pushBack(Link(src));
      }
      break;

    default:
      break;
    }

    return res;
  }

public:
  // The constructor is given a buffer and length containing
  // presumed HTML.  It will parse the buffer, stripping out
  // all the HTML tags and producing the list of words in body,
  // words in title, and links found on the page.
  HtmlParser(const char* buffer, size_t length)
      : curr(buffer), end(buffer + length), curr_in_title(false), curr_in_anchor(false),
        seen_base(false)
  {
    const char* start_word_ptr = nullptr;
    int idx = -1;
    while (curr < end)
    {
      char c = *curr;
      if (c == '<')
      {
        const char* tag_start = curr;
        bool was_in_title = curr_in_title;
        int anchor_idx = idx;
        tag_result res = process_tag();
        if (res != tag_result::not_tag)
        {
          if (start_word_ptr)
          {
            add_to_words(start_word_ptr, tag_start, was_in_title, anchor_idx);
            start_word_ptr = nullptr;
          }
          if (res == tag_result::anchor_open)
          {
            idx = links.size() - 1;
          }
          else if (res == tag_result::anchor_closed)
          {
            idx = -1;
          }
        }
        else
        {
          if (!start_word_ptr)
          {
            start_word_ptr = curr;
          }
          curr++;
        }
      }
      else if (is_whitespace(c))
      {
        if (start_word_ptr)
        {
          add_to_words(start_word_ptr, curr, curr_in_title, idx);
          start_word_ptr = nullptr;
        }
        curr++;
      }
      else
      {
        if (!start_word_ptr)
        {
          start_word_ptr = curr;
        }
        curr++;
      }
    }
    if (start_word_ptr)
    {
      add_to_words(start_word_ptr, curr, curr_in_title, idx);
    }
  }
};
} // namespace avengine
