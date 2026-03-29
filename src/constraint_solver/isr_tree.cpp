#include <avengine/constraint_solver/isr_tree.h>

#include <limits>

namespace avengine
{
namespace constraint_solver
{
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

ISRAnd::ISRAnd(const vector<ISR*>& terms, ISREndDoc* documentEnd)
    : Terms(terms), DocumentEnd(documentEnd)
{
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

  for (;;)
  {
    for (ISR* term : Terms)
    {
      if (!term || !term->Seek(target))
      {
        ClearCurrent();
        currentDocumentEnd_ = 0;
        return nullptr;
      }
    }

    for (;;)
    {
      Location farthestLocation = 0;
      farthestTerm_ = 0;
      for (size_t i = 0; i < Terms.size(); i++)
      {
        if (Terms[i]->GetEndLocation() > farthestLocation)
        {
          farthestLocation = Terms[i]->GetEndLocation();
          farthestTerm_ = static_cast<unsigned>(i);
        }
      }

      if (!DocumentEnd->Seek(farthestLocation))
      {
        ClearCurrent();
        currentDocumentEnd_ = 0;
        return nullptr;
      }

      const Location documentStart = DocumentEnd->GetDocumentStart();
      const Location documentEnd = DocumentEnd->GetEndLocation();
      bool retryNextDocument = false;
      Location nearestLocation = 0;
      Location farthestMatch = 0;
      Attributes nearestAttributes = {};

      for (size_t i = 0; i < Terms.size(); i++)
      {
        ISR* term = Terms[i];
        if (!term->Seek(documentStart))
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

        if (nearestLocation == 0 || term->GetStartLocation() < nearestLocation)
        {
          nearestLocation = term->GetStartLocation();
          nearestTerm_ = static_cast<unsigned>(i);
          nearestAttributes = term->GetCurrentPost()->GetAttributes();
        }

        if (term->GetEndLocation() > farthestMatch)
        {
          farthestMatch = term->GetEndLocation();
          nearestEndLocation_ = farthestMatch;
        }
      }

      if (retryNextDocument)
        break;

      nearestStartLocation_ = nearestLocation;
      nearestEndLocation_ = farthestMatch;
      currentDocumentEnd_ = documentEnd;
      return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
    }
  }
}

Post* ISRAnd::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(currentDocumentEnd_ + 1);
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

ISRAndNot::ISRAndNot(const vector<ISR*>& required, const vector<ISR*>& excluded, ISREndDoc* endDoc)
    : Required(required), Excluded(excluded), EndDoc(endDoc)
{
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

  for (;;)
  {
    Location candidateTarget = target;

    for (;;)
    {
      for (ISR* term : Required)
      {
        if (!term || !term->Seek(candidateTarget))
        {
          ClearCurrent();
          currentDocumentEnd_ = 0;
          return nullptr;
        }
      }

      Location farthestLocation = 0;
      farthestRequired_ = 0;
      for (size_t i = 0; i < Required.size(); i++)
      {
        if (Required[i]->GetEndLocation() > farthestLocation)
        {
          farthestLocation = Required[i]->GetEndLocation();
          farthestRequired_ = static_cast<unsigned>(i);
        }
      }

      if (!EndDoc->Seek(farthestLocation))
      {
        ClearCurrent();
        currentDocumentEnd_ = 0;
        return nullptr;
      }

      const Location documentStart = EndDoc->GetDocumentStart();
      const Location documentEnd = EndDoc->GetEndLocation();
      bool retry = false;
      Location nearestLocation = 0;
      Location farthestMatch = 0;
      Attributes nearestAttributes = {};

      for (size_t i = 0; i < Required.size(); i++)
      {
        ISR* term = Required[i];
        if (!term->Seek(documentStart))
        {
          ClearCurrent();
          currentDocumentEnd_ = 0;
          return nullptr;
        }

        if (term->GetStartLocation() > documentEnd)
        {
          candidateTarget = documentEnd + 1;
          retry = true;
          break;
        }

        if (nearestLocation == 0 || term->GetStartLocation() < nearestLocation)
        {
          nearestLocation = term->GetStartLocation();
          nearestRequired_ = static_cast<unsigned>(i);
          nearestAttributes = term->GetCurrentPost()->GetAttributes();
        }

        if (term->GetEndLocation() > farthestMatch)
          farthestMatch = term->GetEndLocation();
      }

      if (retry)
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
        break;

      nearestStartLocation_ = nearestLocation;
      nearestEndLocation_ = farthestMatch;
      currentDocumentEnd_ = documentEnd;
      return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
    }
  }
}

Post* ISRAndNot::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(currentDocumentEnd_ + 1);
}

} // namespace constraint_solver
} // namespace avengine
