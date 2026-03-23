#pragma once

#include "avengine/avengine_lib/string_slice.h"
#include "avengine/avengine_lib/vector.h"

namespace avengine
{

enum class RejectionReason
{
  NOT_REJECTED = 0,
  TOO_MUCH_NONASCII = 1,
  TOO_MANY_ACCENTS = 2,
  INAPPROPRIATE_WORDS = 3,
};

// Normalizes `words` in-place and checks whether the page should be rejected.
//
// Each word is lowercased, accented Latin characters are folded to their ASCII
// base letter (e.g. `é` → `e`), and non-alphabetic characters are stripped.
//
// Returns `NOT_REJECTED` if the page passes all filters, or the first
// applicable `RejectionReason` if it should be discarded (e.g. too many
// non-ASCII bytes, too many accented characters).
RejectionReason normalizeAndReject(avengine::vector<avengine::string_slice>&);

} // namespace avengine
