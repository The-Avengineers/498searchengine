#ifndef AVENGINE_CONSTRAINT_SOLVER_POSTING_SOURCE_H_
#define AVENGINE_CONSTRAINT_SOLVER_POSTING_SOURCE_H_

#include <cstddef>
#include <cstdint>

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>

namespace avengine
{
namespace constraint_solver
{

// Non-owning view of all positions for one term in one document.
struct DocumentPostingView
{
  uint32_t DocId{0};
  const uint32_t* Positions{nullptr};
  size_t PositionCount{0};
};

// Read-only source of per-term postings.
class PostingSource
{
public:
  virtual ~PostingSource() = default;

  // Fill out with doc-sorted postings for one normalized term.
  virtual bool LookupTerm(const string& term, vector<DocumentPostingView>& out) const = 0;

  // Return the URL or name for one document id.
  virtual string GetDocumentName(uint32_t docId) const = 0;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_POSTING_SOURCE_H_ */
