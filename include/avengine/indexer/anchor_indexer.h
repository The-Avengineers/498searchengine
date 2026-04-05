#pragma once
#include "avengine/avengine_lib/string_slice.h"
#include "avengine/avengine_lib/vector.h"

// Opens (or resumes) the anchor text log file. Called from resumeIndexer() on
// startup. Opens in append mode so a resumed crawl extends the existing log.
void openAnchorLog();

// Appends one record to the anchor text log (ac_log.tsv):
//   <target_url>\t<word> <word> ...\n
//
// Safe to call from multiple threads concurrently. No-op if anchor is empty.
// The log is processed offline after crawling to merge anchor terms into the
// binary index.
void saveAnchorText(const avengine::string& targetUrl,
                    const avengine::vector<avengine::string_slice>& anchor);

// Closes the anchor log. Called from cleanUpIndexer() after all crawler
// threads have finished and the binary index has been serialized.
void closeAnchorLog();
