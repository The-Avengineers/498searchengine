#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace avengine
{

// Strategies are always added in this order:
//   1. exact_phrase
//   2. correct_ordering
//   3. early_query_term
//   4. term_coverage
//   5. query_occurrence
//   6. short_title
//   7. short_url
//   8. domain
//   9. url_depth
//  10. url_path_depth
//  11. title_term_coverage
//  12. seed_domain_depth
//  13. query_param_penalty
//  14. exact_title_match
struct RankerWeights
{
  double exact_phrase{2.4};
  double correct_ordering{1.6};
  double early_query_term{1.25};
  double term_coverage{0.85};
  double query_occurrence{0.8};
  double short_title{0.5};
  double short_url{0.15};
  double domain{0.1};
  double url_depth{0.15};
  double url_path_depth{0.5};
  double title_term_coverage{1.0};
  double seed_domain_depth{1.0};
  double query_param_penalty{-2.0};
  double exact_title_match{3.0};
};

// Load weights from path
// Unknown keys are ignored and  issing keys keep their default values.
// Returns defaults if the file cannot be opened.
inline RankerWeights LoadRankerWeights(const char* path)
{
  RankerWeights w;
  FILE* f = fopen(path, "r");
  if (!f)
    return w;

  char line[256];
  while (fgets(line, static_cast<int>(sizeof(line)), f))
  {
    char* eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';

    char* key = line;
    while (*key == ' ' || *key == '\t')
      ++key;
    char* end = eq - 1;
    while (end > key && (*end == ' ' || *end == '\t'))
      --end;
    *(end + 1) = '\0';

    if (key[0] == '#' || key[0] == '\0')
      continue;

    char* val = eq + 1;
    while (*val == ' ' || *val == '\t')
      ++val;
    end = val + strlen(val) - 1;
    while (end >= val && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
      --end;
    *(end + 1) = '\0';

    const double v = strtod(val, nullptr);

    if (strcmp(key, "exact_phrase") == 0)
      w.exact_phrase = v;
    else if (strcmp(key, "correct_ordering") == 0)
      w.correct_ordering = v;
    else if (strcmp(key, "early_query_term") == 0)
      w.early_query_term = v;
    else if (strcmp(key, "term_coverage") == 0)
      w.term_coverage = v;
    else if (strcmp(key, "query_occurrence") == 0)
      w.query_occurrence = v;
    else if (strcmp(key, "short_title") == 0)
      w.short_title = v;
    else if (strcmp(key, "short_url") == 0)
      w.short_url = v;
    else if (strcmp(key, "domain") == 0)
      w.domain = v;
    else if (strcmp(key, "url_depth") == 0)
      w.url_depth = v;
    else if (strcmp(key, "url_path_depth") == 0)
      w.url_path_depth = v;
    else if (strcmp(key, "title_term_coverage") == 0)
      w.title_term_coverage = v;
    else if (strcmp(key, "seed_domain_depth") == 0)
      w.seed_domain_depth = v;
    else if (strcmp(key, "query_param_penalty") == 0)
      w.query_param_penalty = v;
    else if (strcmp(key, "exact_title_match") == 0)
      w.exact_title_match = v;
  }

  fclose(f);
  return w;
}

} // namespace avengine
