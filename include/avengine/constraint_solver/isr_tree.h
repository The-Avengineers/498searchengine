#ifndef AVENGINE_CONSTRAINT_SOLVER_ISR_TREE_H_
#define AVENGINE_CONSTRAINT_SOLVER_ISR_TREE_H_

#include <avengine/index_stream_reader.h>

namespace avengine
{
namespace constraint_solver
{

class ISROr : public ISR
{
public:
  ISROr() = default;
  explicit ISROr(const vector<ISR*>& terms);

  Post* Seek(Location target) override;
  Post* Next() override;
  Post* NextDocument();

  vector<ISR*> Terms;

private:
  Post* RefreshCurrentFromChildren();

  unsigned nearestTerm_ = 0;
  Location nearestStartLocation_ = 0;
  Location nearestEndLocation_ = 0;
};

class ISRAnd : public ISR
{
public:
  ISRAnd() = default;
  ISRAnd(const vector<ISR*>& terms, ISREndDoc* documentEnd);

  Post* Seek(Location target) override;
  Post* Next() override;

  vector<ISR*> Terms;
  ISREndDoc* DocumentEnd = nullptr;

private:
  unsigned nearestTerm_ = 0;
  unsigned farthestTerm_ = 0;
  Location nearestStartLocation_ = 0;
  Location nearestEndLocation_ = 0;
  Location currentDocumentEnd_ = 0;
};

class ISRPhrase : public ISR
{
public:
  ISRPhrase() = default;
  explicit ISRPhrase(const vector<ISR*>& terms, ISREndDoc* documentEnd = nullptr);

  Post* Seek(Location target) override;
  Post* Next() override;

  vector<ISR*> Terms;
  ISREndDoc* DocumentEnd = nullptr;
};

class ISRAndNot : public ISR
{
public:
  ISRAndNot() = default;
  ISRAndNot(const vector<ISR*>& required, const vector<ISR*>& excluded, ISREndDoc* endDoc);

  Post* Seek(Location target) override;
  Post* Next() override;

  vector<ISR*> Required;
  vector<ISR*> Excluded;
  ISREndDoc* EndDoc = nullptr;

private:
  unsigned nearestRequired_ = 0;
  unsigned farthestRequired_ = 0;
  Location nearestStartLocation_ = 0;
  Location nearestEndLocation_ = 0;
  Location currentDocumentEnd_ = 0;
};

} // namespace constraint_solver
} // namespace avengine

#endif /* AVENGINE_CONSTRAINT_SOLVER_ISR_TREE_H_ */
