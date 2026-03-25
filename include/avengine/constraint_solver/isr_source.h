#ifndef AVENGINE_CONSTRAINT_SOLVER_ISR_SOURCE_H_
#define AVENGINE_CONSTRAINT_SOLVER_ISR_SOURCE_H_

#include <cstdint>

#include <avengine/avengine_lib/string.h>
#include <avengine/index_stream_reader.h>

namespace avengine
{
namespace constraint_solver
{

class IsrSource
{
public:
  virtual ~IsrSource() = default;

  // Returns a heap-allocated ISRWord. The caller owns the result and must
  // delete it unless ownership is immediately transferred to another owner.
  [[nodiscard]] virtual ISRWord* OpenTermIsr(const string& term) const = 0;
  // Returns a heap-allocated ISREndDoc. The caller owns the result and must
  // delete it unless ownership is immediately transferred to another owner.
  [[nodiscard]] virtual ISREndDoc* OpenEndDocIsr() const = 0;
  virtual string GetDocumentUrl(uint32_t docId) const = 0;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_ISR_SOURCE_H_ */
