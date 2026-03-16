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

  virtual ISRWord* OpenTermIsr(const string& term) const = 0;
  virtual ISREndDoc* OpenEndDocIsr() const = 0;
  virtual string GetDocumentName(uint32_t docId) const = 0;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_ISR_SOURCE_H_ */
