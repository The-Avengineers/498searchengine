#include <avengine/constraint_solver/isr_tree.h>

#include <limits>

namespace avengine
{
namespace constraint_solver
{
namespace
{
unsigned MinDocumentCount(const vector<ISR*>& terms)
{
  unsigned best = std::numeric_limits<unsigned>::max();
  for (ISR* term : terms)
  {
    if (!term)
      continue;
    const unsigned count = term->GetDocumentCount();
    if (count < best)
      best = count;
  }
  return best;
}

void SortByRarity(vector<ISR*>& terms)
{
  terms.sort([](const ISR* lhs, const ISR* rhs)
  {
    const unsigned lhsCount = lhs ? lhs->GetDocumentCount() : std::numeric_limits<unsigned>::max();
    const unsigned rhsCount = rhs ? rhs->GetDocumentCount() : std::numeric_limits<unsigned>::max();
    return lhsCount < rhsCount;
  });
}
} // namespace

ISROr::ISROr(const vector<ISR*>& terms) : Terms(terms)
{
  RefreshCurrentFromChildren();
}

Post* ISROr::RefreshCurrentFromChildren()
{
  size_t bestIndex = Terms.size();
  Location bestStart = 0;
  Location bestEnd = 0;
  Attributes bestAttributes = {};

  for (size_t i = 0; i < Terms.size(); i++)
  {
    ISR* term = Terms[i];
    if (!term)
      continue;

    Post* current = term->GetCurrentPost();
    if (!current)
      continue;

    const Location start = current->GetStartLocation();
    const Location end = current->GetEndLocation();
    if (bestIndex == Terms.size() || start < bestStart || (start == bestStart && end < bestEnd))
    {
      bestIndex = i;
      bestStart = start;
      bestEnd = end;
      bestAttributes = current->GetAttributes();
    }
  }

  if (bestIndex == Terms.size())
  {
    ClearCurrent();
    nearestTerm_ = 0;
    nearestStartLocation_ = 0;
    nearestEndLocation_ = 0;
    return nullptr;
  }

  nearestTerm_ = static_cast<unsigned>(bestIndex);
  nearestStartLocation_ = bestStart;
  nearestEndLocation_ = bestEnd;
  return SetCurrent(bestStart, bestEnd, bestAttributes);
}

Post* ISROr::Seek(Location target)
{
  for (ISR* term : Terms)
  {
    if (term)
      term->Seek(target);
  }

  return RefreshCurrentFromChildren();
}

Post* ISROr::Next()
{
  if (!hasCurrentPost_ || nearestTerm_ >= Terms.size())
    return nullptr;

  if (Terms[nearestTerm_])
    Terms[nearestTerm_]->Next();

  return RefreshCurrentFromChildren();
}

Post* ISROr::NextDocument()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(GetEndLocation() + 1);
}

unsigned ISROr::GetDocumentCount() const
{
  uint64_t total = 0;
  for (ISR* term : Terms)
  {
    if (!term)
      continue;
    total += term->GetDocumentCount();
    if (total >= std::numeric_limits<unsigned>::max())
      return std::numeric_limits<unsigned>::max();
  }
  return static_cast<unsigned>(total);
}

ISRAnd::ISRAnd(const vector<ISR*>& terms, ISREndDoc* documentEnd)
    : Terms(terms), DocumentEnd(documentEnd)
{
  SortByRarity(Terms);
}

Post* ISRAnd::Seek(Location target)
{
  if (!DocumentEnd || Terms.empty())
  {
    ClearCurrent();
    currentDocumentEnd_ = 0;
    return nullptr;
  }

  if (target == 0)
    target = 1;

  ISR* driver = Terms[0];
  for (;;)
  {
    if (!driver || !driver->Seek(target))
    {
      ClearCurrent();
      currentDocumentEnd_ = 0;
      return nullptr;
    }

    if (!DocumentEnd->Seek(driver->GetEndLocation()))
    {
      ClearCurrent();
      currentDocumentEnd_ = 0;
      return nullptr;
    }

    const Location documentStart = DocumentEnd->GetDocumentStart();
    const Location documentEnd = DocumentEnd->GetEndLocation();
    Location nearestLocation = driver->GetStartLocation();
    Location farthestMatch = driver->GetEndLocation();
    nearestTerm_ = 0;
    farthestTerm_ = 0;
    Attributes nearestAttributes = driver->GetCurrentPost()->GetAttributes();
    bool retryNextDocument = false;

    for (size_t i = 1; i < Terms.size(); i++)
    {
      ISR* term = Terms[i];
      if (!term || !term->Seek(documentStart))
      {
        ClearCurrent();
        currentDocumentEnd_ = 0;
        return nullptr;
      }

      if (term->GetStartLocation() > documentEnd)
      {
        target = documentEnd + 1;
        retryNextDocument = true;
        break;
      }

      if (term->GetStartLocation() < nearestLocation)
      {
        nearestLocation = term->GetStartLocation();
        nearestTerm_ = static_cast<unsigned>(i);
        nearestAttributes = term->GetCurrentPost()->GetAttributes();
      }

      if (term->GetEndLocation() > farthestMatch)
      {
        farthestMatch = term->GetEndLocation();
        farthestTerm_ = static_cast<unsigned>(i);
      }
    }

    if (retryNextDocument)
      continue;

    nearestStartLocation_ = nearestLocation;
    nearestEndLocation_ = farthestMatch;
    currentDocumentEnd_ = documentEnd;
    return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
  }
}

Post* ISRAnd::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(currentDocumentEnd_ + 1);
}

unsigned ISRAnd::GetDocumentCount() const
{
  return MinDocumentCount(Terms);
}

ISRPhrase::ISRPhrase(const vector<ISR*>& terms, ISREndDoc* documentEnd)
    : Terms(terms), DocumentEnd(documentEnd)
{
}

Post* ISRPhrase::Seek(Location target)
{
  if (Terms.empty())
  {
    ClearCurrent();
    return nullptr;
  }

  if (target == 0)
    target = 1;

  for (size_t i = 0; i < Terms.size(); i++)
  {
    if (!Terms[i] || !Terms[i]->Seek(target + i))
    {
      ClearCurrent();
      return nullptr;
    }
  }

  for (;;)
  {
    Location minBase = std::numeric_limits<Location>::max();
    Location maxBase = 0;

    for (size_t i = 0; i < Terms.size(); i++)
    {
      const Location start = Terms[i]->GetStartLocation();
      const Location base = start >= i ? (start - i) : 0;
      if (base < minBase)
        minBase = base;
      if (base > maxBase)
        maxBase = base;
    }

    if (minBase == maxBase)
    {
      const Location phraseStart = maxBase;
      const Location phraseEnd = phraseStart + Terms.size() - 1;

      if (DocumentEnd)
      {
        if (!DocumentEnd->Seek(phraseEnd))
        {
          ClearCurrent();
          return nullptr;
        }

        if (phraseStart < DocumentEnd->GetDocumentStart())
        {
          target = DocumentEnd->GetDocumentStart();
          for (size_t i = 0; i < Terms.size(); i++)
          {
            if (!Terms[i]->Seek(target + i))
            {
              ClearCurrent();
              return nullptr;
            }
          }
          continue;
        }
      }

      return SetCurrent(phraseStart, phraseEnd, Terms[0]->GetCurrentPost()->GetAttributes());
    }

    for (size_t i = 0; i < Terms.size(); i++)
    {
      const Location start = Terms[i]->GetStartLocation();
      const Location base = start >= i ? (start - i) : 0;
      if (base < maxBase && !Terms[i]->Seek(maxBase + i))
      {
        ClearCurrent();
        return nullptr;
      }
    }
  }
}

Post* ISRPhrase::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(GetStartLocation() + 1);
}

unsigned ISRPhrase::GetDocumentCount() const
{
  return MinDocumentCount(Terms);
}

ISRAndNot::ISRAndNot(const vector<ISR*>& required, const vector<ISR*>& excluded, ISREndDoc* endDoc)
    : Required(required), Excluded(excluded), EndDoc(endDoc)
{
  SortByRarity(Required);
}

Post* ISRAndNot::Seek(Location target)
{
  if (!EndDoc || Required.empty())
  {
    ClearCurrent();
    currentDocumentEnd_ = 0;
    return nullptr;
  }

  if (target == 0)
    target = 1;

  ISR* driver = Required[0];
  for (;;)
  {
    if (!driver || !driver->Seek(target))
    {
      ClearCurrent();
      currentDocumentEnd_ = 0;
      return nullptr;
    }

    if (!EndDoc->Seek(driver->GetEndLocation()))
    {
      ClearCurrent();
      currentDocumentEnd_ = 0;
      return nullptr;
    }

    const Location documentStart = EndDoc->GetDocumentStart();
    const Location documentEnd = EndDoc->GetEndLocation();
    Location nearestLocation = driver->GetStartLocation();
    Location farthestMatch = driver->GetEndLocation();
    nearestRequired_ = 0;
    farthestRequired_ = 0;
    Attributes nearestAttributes = driver->GetCurrentPost()->GetAttributes();
    bool retryNextDocument = false;

    for (size_t i = 1; i < Required.size(); i++)
    {
      ISR* term = Required[i];
      if (!term || !term->Seek(documentStart))
      {
        ClearCurrent();
        currentDocumentEnd_ = 0;
        return nullptr;
      }

      if (term->GetStartLocation() > documentEnd)
      {
        target = documentEnd + 1;
        retryNextDocument = true;
        break;
      }

      if (term->GetStartLocation() < nearestLocation)
      {
        nearestLocation = term->GetStartLocation();
        nearestRequired_ = static_cast<unsigned>(i);
        nearestAttributes = term->GetCurrentPost()->GetAttributes();
      }

      if (term->GetEndLocation() > farthestMatch)
      {
        farthestMatch = term->GetEndLocation();
        farthestRequired_ = static_cast<unsigned>(i);
      }
    }

    if (retryNextDocument)
      continue;

    bool excludedHit = false;
    for (ISR* term : Excluded)
    {
      if (!term)
        continue;

      Post* excluded = term->Seek(documentStart);
      if (excluded && term->GetStartLocation() <= documentEnd)
      {
        target = documentEnd + 1;
        excludedHit = true;
        break;
      }
    }

    if (excludedHit)
      continue;

    nearestStartLocation_ = nearestLocation;
    nearestEndLocation_ = farthestMatch;
    currentDocumentEnd_ = documentEnd;
    return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
  }
}

Post* ISRAndNot::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(currentDocumentEnd_ + 1);
}

unsigned ISRAndNot::GetDocumentCount() const
{
  return MinDocumentCount(Required);
}

} // namespace constraint_solver
} // namespace avengine
