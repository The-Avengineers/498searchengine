// HtmlParser.h
// Nicole Hamilton, nham@umich.edu

#pragma once

#include <vector>
#include "HtmlTags.h"

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
//      <-- is the beginng of a comment.  Everything up to the ending -->
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
      std::string URL;
      std::vector< std::string > anchorText;

      Link( std::string URL ) : URL( URL )
         {
         }
   };


class HtmlParser
   {
   public:

      std::vector< std::string > words, titleWords;
      std::vector< Link > links;
      std::string base;

   private:
      // curr ptr
      const char* curr;
      // end ptr
      const char* end;

      bool curr_in_title;
      bool curr_in_anchor;


      void add_to_words(const char *start_, const char *end_) {
            
         std::string word(start_, end_ - start_);
         if (curr_in_title) {
            titleWords.push_back(word);
         }
         else {
            words.push_back(word);
         }
         if (curr_in_anchor && !links.empty()) {
            links.back().anchorText.push_back(word);
         }

      }

      void discard_section(const char* lowercaseName, size_t len) {
         while (curr < end) {
            if (*curr == '<' && curr + 1 < end && *(curr + 1) == '/') {

               const char *tag_start = curr + 2;
               const char *p = tag_start;
               while (p < end && *p != '>' && *p != '/' && (*p) != ' ') {
                  p++;
               }
               
               size_t new_len = p - tag_start;
               if (new_len == len) {
                  bool match = true;
                  for (size_t i = 0; i < len && match; i++) {
                     if (tolower(tag_start[i]) != lowercaseName[i]) {
                        match = false;
                     }
                  }
                  
                  if (match) {
                     while (curr < end && *curr != '>') {
                        curr++;
                     }
                     if (curr < end) {
                        curr++; 
                     }
                     return;
                  }
               }
            }
            curr++;
         }
      }

      std::string get_src(const char *start, const char *tag_end) {
         const char *p = start;
         
         while (p + 4 < tag_end) {
            if (tolower(*p) == 's' && tolower(p[1]) == 'r' && tolower(p[2]) == 'c') {
               p += 3;
               
               while (p < tag_end && *p == ' ') {
                  p++;
               }
               
               if (p < tag_end && *p == '=') {
                  p++; 
                  
                  while (p < tag_end && *p == ' ') {
                     p++;
                  }
                  
                  if (p < tag_end) {
                     char quote = *p;
                     if (quote == '"' || quote == '\'') {
                        p++; 
                        const char *valueStart = p;
               
                        while (p < tag_end && *p != quote) {
                           p++;
                        }
                        return std::string(valueStart, p - valueStart);
                     }
                     else {
                        const char *valueStart = p;
                        while (p < tag_end && *p != ' ' && *p != '>') {
                           p++;
                        }
                        
                        return std::string (valueStart, p - valueStart);
                     }
                  }
               }
            }
            p++;
         }
         return std::string();
      }

      std::string get_href(const char *start, const char *tag_end) {
         const char *p = start;
         
         while (p + 5 < tag_end) {
            if (tolower(*p) == 'h' && tolower(p[1]) == 'r' &&
               tolower(p[2]) == 'e' && tolower(p[3]) == 'f') {

               p += 4; 
               while (p < tag_end && *p == ' ') {
                  p++;
               }
               if (p < tag_end && *p == '=') {
                  p++;
                  
                  while (p < tag_end && *p == ' ') {
                     p++;
                  }
                  
                  if (p < tag_end) {
                     char quote = *p;
                     if (quote == '"' || quote == '\'') {
                        p++;
                        const char *value_start = p;
                        while (p < tag_end && *p != quote) {
                           p++;
                        }
                        return std::string(value_start, p - value_start);
                     }
                     else {
                        const char *value_start = p;
                        while (p < tag_end && *p != ' ' && *p != '>') {
                           p++;
                        }
                        return std::string(value_start, p - value_start);
                     }
                  }
               }
            }
            p++;
         }
         return std::string();
         
      }

      bool process_tag() {
         const char *start_of_tag = curr;
         curr++;
         
         if (curr >= end) {
            curr = start_of_tag;
            return false;
         }
         
         bool is_closing = false;
         if (*curr == '/') {
            is_closing = true;
            curr++;
            if (curr >= end) {
               curr = start_of_tag;
               return false;
            }
         }
         
         const char *start_of_name = curr;
         
      
         if (!is_closing && curr + 2 < end && *curr == '!' && *(curr + 1) == '-' 
               && *(curr + 2) == '-') {

            curr += 3; // skip !-- bc comment
            while (curr < end) {
               if (*curr == '-' && curr + 2 < end && *(curr + 1) == '-' 
                     && *(curr + 2) == '>') {

                  curr += 3;
                  break;
                  }
               curr++;
            }
            return true;
         }
         
         // read tag
         while (curr < end && *curr != '>' &&  *curr != '/' && *curr != ' ') {
            curr++;
         }
         
         const char *name_end = curr;
         
         // empty tag
         if (start_of_name == name_end) {
            curr = start_of_tag;
            return false;
         }
         
         size_t name_len = name_end - start_of_name;
         char lowercase_name[32];
         if (name_len >= sizeof(lowercase_name)) {
            // wayy to long
            curr = start_of_tag;
            return false;
         }
         
         for (size_t i = 0; i < name_len; i++) {
            lowercase_name[i] = tolower(start_of_name[i]);
         }
         lowercase_name[name_len] = '\0';
         
         // get action
         DesiredAction action = LookupPossibleTag(lowercase_name, lowercase_name + name_len);
         
         if (action == DesiredAction::OrdinaryText) {
            curr = start_of_tag;
            return false;
            }
         
         const char *attr_start = curr;
         while (curr < end && *curr != '>') {
            curr++;
         }
         const char *tag_end = curr;
         
         if (curr < end) {
            curr++; 
         }
         
         // Handle the tag based on its action
         switch (action) {
            case DesiredAction::Discard:
               break;
               
            case DesiredAction::DiscardSection:
               // discard everything
               if (!is_closing) {
                  discard_section(lowercase_name, name_len);
               }
               break;
               
            case DesiredAction::Title:
               if (is_closing) {
                  curr_in_title = false;
               }
               else {
                  curr_in_title = true;
               }
               break;
               
            case DesiredAction::Anchor:
               if (is_closing) {
                  curr_in_anchor = false;
               }
               else {
                  std::string href = get_href(attr_start, tag_end);
                  if (!href.empty()) {
                     links.push_back(Link(href));
                     curr_in_anchor = true;
                  }
               }
               break;
               
            case DesiredAction::Base:
               if (!is_closing && base.empty()) {
                  std::string href = get_href(attr_start, tag_end);
                  base = href;
                  
               }
               break;
               
            case DesiredAction::Embed:
               if (!is_closing) {
                  std::string src = get_src(attr_start, tag_end);
                  if (!src.empty())
                     links.push_back(Link(src));
                  }
               break;
               
            default:
               break;
            }
         
         return true;
   }     


   public:

      // The constructor is given a buffer and length containing
      // presumed HTML.  It will parse the buffer, stripping out
      // all the HTML tags and producing the list of words in body,
      // words in title, and links found on the page.

      HtmlParser( const char *buffer, size_t length ) 
            : curr(buffer), end(buffer + length), curr_in_title(false),
            curr_in_anchor(false)
         {

            const char* start_word_ptr = nullptr;
            while (curr < end) {
               char c = *curr;
               if (c == '<') {
                  if (start_word_ptr) {
                     add_to_words(start_word_ptr, curr);
                     start_word_ptr = nullptr;
                  }
                  if (!process_tag()) {
                     if (!start_word_ptr) {
                        start_word_ptr = curr;
                     }
                     curr++;
                  }
               }
               else if (c == ' ') {
                  if (start_word_ptr) {
                     add_to_words(start_word_ptr, curr);
                     start_word_ptr = nullptr;
                  }
                  curr++;
               }
               else {
                  if (!start_word_ptr) {
                     start_word_ptr = curr;
                  }
                  curr++;
               }
            }
            if (start_word_ptr) {
               add_to_words(start_word_ptr, curr);
            }

         }
   };
