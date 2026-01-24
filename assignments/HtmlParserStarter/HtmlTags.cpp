#include <ctype.h>
#include <cstring>
#include <cassert>
#include "HtmlTags.h"

// name points to beginning of the possible HTML tag name.
// nameEnd points to one past last character.
// Comparison is case-insensitive.
// Use a binary search.
// If the name is found in the TagsRecognized table, return
// the corresponding action.
// If the name is not found, return OrdinaryText.

// compare words to find tag
// requires start and end pointer for the name and the start pointer for tag
// modifys nothing
// returns positive number if name is longer than tag or characters dont match
// returns negative if name is shorter than tag or characters dont match
// returns 0 if they match
[[gnu::always_inline]] inline int compare(const char *name, const char *nameEnd, 
      const char *tag) {

         // loop through length of the name and hope they match
         while (name < nameEnd && *tag) 
         {
            const char name_curr = *name;
            const char tag_curr = *tag;
            if (tag_curr != name_curr) {
               // if they dont match return alphabetical distance
               return name_curr - tag_curr;
            }
            name++;
            tag++;
         }
         if (name < nameEnd) {
            return 1;
         }
         else if (*tag) {
            return -1;
         }
         else {
            return 0;
         }
   }

// binary search to find a tag
// requires start and end pointer for the name and the start pointer for tag
// modifys nothing
// returns desired action for the correct tag
DesiredAction LookupPossibleTag( const char *name, const char *nameEnd )
   {

   if (!nameEnd) {
      nameEnd = name + strlen(name);
   }
   int l = 0;
   int r = NumberOfTags - 1;

   while (l <= r) 
   {
      int m = l + r;
      m /= 2;
      const char* tag = TagsRecognized[m].Tag;

      int val = compare(name, nameEnd, tag);
      if (val == 0) {
         return TagsRecognized[m].Action;
      }
      else if (val < 0) {
         r = m - 1;
      }
      else {
         l = m + 1;
      }
   }


   return DesiredAction::OrdinaryText;
}