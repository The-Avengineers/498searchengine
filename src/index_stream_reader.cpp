#include "avengine/index_stream_reader.h"

#include <algorithm>
#include <limits>

namespace avengine
{
namespace
{
static uint64_t read_u64_le(const uint8_t* ptr)
{
  uint64_t value = 0;
  for (int i = 0; i < 8; i++)
    value |= static_cast<uint64_t>(ptr[i]) << (i * 8);
  return value;
}

static uint32_t read_var_u32(const uint8_t* begin, const uint8_t* end, const uint8_t** next)
{
  uint32_t result = 0;
  uint32_t shift = 0;

  while (begin < end && shift < 35)
  {
    const uint8_t byte = *begin++;
    result |= static_cast<uint32_t>(byte & 0x7Fu) << shift;
    if ((byte & 0x80u) == 0u)
    {
      *next = begin;
      return result;
    }
    shift += 7;
  }

  *next = nullptr;
  return 0;
}

static bool skip_bytes(const uint8_t** cursor, const uint8_t* end, uint32_t count)
{
  if (count > static_cast<uint32_t>(end - *cursor))
    return false;

  *cursor += count;
  return true;
}

static bool find_document_for_location(const vector<DocumentInfo>& documents, Location location,
                                       DocumentInfo* out, size_t* outIndex = nullptr)
{
  if (location == 0 || documents.empty())
    return false;

  size_t left = 0;
  size_t right = documents.size();

  while (left < right)
  {
    const size_t mid = left + ((right - left) / 2);
    if (documents[mid].end < location)
      left = mid + 1;
    else
      right = mid;
  }

  if (left >= documents.size())
    return false;

  const DocumentInfo& doc = documents[left];
  if (location < doc.start || location > doc.end)
    return false;

  if (out)
    *out = doc;
  if (outIndex)
    *outIndex = left;
  return true;
}

static Attributes make_word_attributes(const vector<DocumentInfo>& documents, Location location,
                                       uint32_t documentCount)
{
  Attributes attributes = {};
  attributes.kind = Attributes::Kind::WORD;
  attributes.Word.documentFrequency = documentCount;

  DocumentInfo doc = {};
  if (find_document_for_location(documents, location, &doc))
    attributes.Word.documentId = doc.documentId;

  return attributes;
}

[[maybe_unused]] static Attributes make_document_attributes(const DocumentInfo& document)
{
  Attributes attributes = {};
  attributes.kind = Attributes::Kind::DOCUMENT;
  attributes.Document.documentId = document.documentId;
  attributes.Document.documentLength =
      document.end >= document.start ? static_cast<uint32_t>(document.end - document.start + 1) : 0;
  attributes.Document.titleLength = document.titleLength;
  attributes.Document.urlLength = document.urlLength;
  return attributes;
}
} // namespace

Post* ISR::GetCurrentPost()
{
  return hasCurrentPost_ ? &currentPost_ : nullptr;
}

Location ISR::GetStartLocation() const
{
  return hasCurrentPost_ ? currentPost_.GetStartLocation() : 0;
}

Location ISR::GetEndLocation() const
{
  return hasCurrentPost_ ? currentPost_.GetEndLocation() : 0;
}

void ISR::ClearCurrent()
{
  hasCurrentPost_ = false;
  currentPost_ = Post();
}

Post* ISR::SetCurrent(Location start, Location end, const Attributes& attributes)
{
  currentPost_.Set(start, end, attributes);
  hasCurrentPost_ = true;
  return &currentPost_;
}

ISRWord::ISRWord(const vector<Location>& occurrences, const vector<DocumentInfo>& documents,
                 uint32_t documentCount)
    : occurrences_(occurrences), documents_(documents), documentCount_(documentCount),
      currentIndex_(0), exhausted_(occurrences.empty())
{
  if (!exhausted_)
    SetCurrentFromIndex(0);
  else
    ClearCurrent();
}

Post* ISRWord::SetCurrentFromIndex(size_t index)
{
  if (index >= occurrences_.size())
  {
    currentIndex_ = occurrences_.size();
    exhausted_ = true;
    ClearCurrent();
    return nullptr;
  }

  currentIndex_ = index;
  exhausted_ = false;
  const Location location = occurrences_[index];
  return SetCurrent(location, location, make_word_attributes(documents_, location, documentCount_));
}

bool ISRWord::ResolveCurrentDocument(DocumentInfo* out) const
{
  if (!hasCurrentPost_)
    return false;

  return find_document_for_location(documents_, currentPost_.GetStartLocation(), out);
}

Post* ISRWord::Next()
{
  if (exhausted_ || occurrences_.empty())
    return nullptr;

  return SetCurrentFromIndex(currentIndex_ + 1);
}

Post* ISRWord::Seek(Location target)
{
  if (occurrences_.empty())
  {
    exhausted_ = true;
    ClearCurrent();
    return nullptr;
  }

  if (target == 0)
    target = 1;

  const Location* found = std::lower_bound(occurrences_.begin(), occurrences_.end(), target);
  if (found == occurrences_.end())
  {
    currentIndex_ = occurrences_.size();
    exhausted_ = true;
    ClearCurrent();
    return nullptr;
  }

  return SetCurrentFromIndex(static_cast<size_t>(found - occurrences_.begin()));
}

Location ISRWord::GetStartLocation() const
{
  return ISR::GetStartLocation();
}

Location ISRWord::GetEndLocation() const
{
  return ISR::GetEndLocation();
}

unsigned ISRWord::GetNumberOfOccurrences() const
{
  return static_cast<unsigned>(occurrences_.size());
}

unsigned ISRWord::GetDocumentCount() const
{
  return documentCount_;
}

ISREndDoc::ISREndDoc(const vector<DocumentInfo>& documents)
{
  documents_ = documents;
  documentCount_ = static_cast<uint32_t>(documents.size());

  for (const DocumentInfo& document : documents)
    occurrences_.pushBack(document.end);

  currentIndex_ = 0;
  exhausted_ = occurrences_.empty();
  if (!exhausted_)
    SetCurrentFromIndex(0);
  else
    ClearCurrent();
}

unsigned ISREndDoc::GetDocumentLength() const
{
  DocumentInfo doc = {};
  if (!ResolveCurrentDocument(&doc))
    return 0;

  return static_cast<unsigned>(doc.end - doc.start + 1);
}

unsigned ISREndDoc::GetTitleLength() const
{
  DocumentInfo doc = {};
  if (!ResolveCurrentDocument(&doc))
    return 0;

  return doc.titleLength;
}

unsigned ISREndDoc::GetUrlLength() const
{
  DocumentInfo doc = {};
  if (!ResolveCurrentDocument(&doc))
    return 0;

  return doc.urlLength;
}

uint32_t ISREndDoc::GetCurrentDocumentId() const
{
  DocumentInfo doc = {};
  if (!ResolveCurrentDocument(&doc))
    return 0;

  return doc.documentId;
}

Location ISREndDoc::GetDocumentStart() const
{
  DocumentInfo doc = {};
  if (!ResolveCurrentDocument(&doc))
    return 0;

  return doc.start;
}

ISROr::ISROr(const vector<ISR*>& terms)
    : Terms(terms), NumberOfTerms(static_cast<unsigned>(terms.size()))
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

unsigned ISROr::GetNumberOfOccurrences() const
{
  return NumberOfTerms;
}

ISRAnd::ISRAnd(const vector<ISR*>& terms, ISREndDoc* documentEnd)
    : Terms(terms), NumberOfTerms(static_cast<unsigned>(terms.size())), DocumentEnd(documentEnd)
{
}

Post* ISRAnd::Seek(Location target)
{
  if (!DocumentEnd || Terms.empty())
  {
    ClearCurrent();
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
        return nullptr;
      }
    }

    for (;;)
    {
      Location farthestLocation = 0;
      farthestTerm_ = 0;
      for (size_t i = 0; i < Terms.size(); i++)
      {
        if (Terms[i] && Terms[i]->GetEndLocation() > farthestLocation)
        {
          farthestLocation = Terms[i]->GetEndLocation();
          farthestTerm_ = static_cast<unsigned>(i);
        }
      }

      if (!DocumentEnd->Seek(farthestLocation))
      {
        ClearCurrent();
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
        if (!term || !term->Seek(documentStart))
        {
          ClearCurrent();
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
      return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
    }
  }
}

Post* ISRAnd::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(nearestStartLocation_ + 1);
}

unsigned ISRAnd::GetNumberOfOccurrences() const
{
  return NumberOfTerms;
}

ISRPhrase::ISRPhrase(const vector<ISR*>& terms, ISREndDoc* documentEnd)
    : Terms(terms), NumberOfTerms(static_cast<unsigned>(terms.size())), DocumentEnd(documentEnd)
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

unsigned ISRPhrase::GetNumberOfOccurrences() const
{
  return NumberOfTerms;
}

ISRContainer::ISRContainer(const vector<ISR*>& contained, const vector<ISR*>& excluded,
                           ISREndDoc* endDoc)
    : Contained(contained), Excluded(excluded), EndDoc(endDoc),
      CountContained(static_cast<unsigned>(contained.size())),
      CountExcluded(static_cast<unsigned>(excluded.size()))
{
}

Post* ISRContainer::Seek(Location target)
{
  if (!EndDoc || Contained.empty())
  {
    ClearCurrent();
    return nullptr;
  }

  if (target == 0)
    target = 1;

  for (;;)
  {
    Location candidateTarget = target;

    for (;;)
    {
      for (ISR* term : Contained)
      {
        if (!term || !term->Seek(candidateTarget))
        {
          ClearCurrent();
          return nullptr;
        }
      }

      Location farthestLocation = 0;
      farthestContained_ = 0;
      for (size_t i = 0; i < Contained.size(); i++)
      {
        if (Contained[i] && Contained[i]->GetEndLocation() > farthestLocation)
        {
          farthestLocation = Contained[i]->GetEndLocation();
          farthestContained_ = static_cast<unsigned>(i);
        }
      }

      if (!EndDoc->Seek(farthestLocation))
      {
        ClearCurrent();
        return nullptr;
      }

      const Location documentStart = EndDoc->GetDocumentStart();
      const Location documentEnd = EndDoc->GetEndLocation();
      bool retry = false;
      Location nearestLocation = 0;
      Location farthestMatch = 0;
      Attributes nearestAttributes = {};

      for (size_t i = 0; i < Contained.size(); i++)
      {
        ISR* term = Contained[i];
        if (!term || !term->Seek(documentStart))
        {
          ClearCurrent();
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
          nearestContained_ = static_cast<unsigned>(i);
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
      return SetCurrent(nearestStartLocation_, nearestEndLocation_, nearestAttributes);
    }
  }
}

Post* ISRContainer::Next()
{
  if (!hasCurrentPost_)
    return nullptr;

  return Seek(GetStartLocation() + 1);
}

unsigned ISRContainer::GetNumberOfOccurrences() const
{
  return CountContained;
}

void Dictionary::AddDocument(Location endLocation, uint32_t titleLength, uint32_t urlLength)
{
  const Location startLocation = documents_.empty() ? 1 : (documents_.back().end + 1);
  if (endLocation < startLocation)
    return;

  documents_.pushBack({static_cast<uint32_t>(documents_.size()), startLocation, endLocation,
                       titleLength, urlLength});
}

void Dictionary::AddOccurrence(const char* token, Location location)
{
  if (!token || location == 0)
    return;

  termLocations_[token].pushBack(location);
  totalWords_++;
}

ISRWord* Dictionary::OpenIsr(const char* token) const
{
  if (!token)
    return nullptr;

  auto found = termLocations_.find(token);
  if (found == termLocations_.end())
    return nullptr;

  uint32_t documentCount = 0;
  uint32_t lastDocumentId = std::numeric_limits<uint32_t>::max();
  for (Location location : found->second)
  {
    DocumentInfo document = {};
    if (find_document_for_location(documents_, location, &document))
    {
      if (document.documentId != lastDocumentId)
      {
        lastDocumentId = document.documentId;
        documentCount++;
      }
    }
  }

  if (documentCount == 0 && !found->second.empty())
    documentCount = 1;

  return new ISRWord(found->second, documents_, documentCount);
}

ISREndDoc* Dictionary::OpenEndDocIsr() const
{
  if (documents_.empty())
    return nullptr;

  return new ISREndDoc(documents_);
}

size_t Dictionary::GetNumberOfWords() const
{
  return totalWords_;
}

size_t Dictionary::GetNumberOfUniqueWords() const
{
  return termLocations_.size();
}

size_t Dictionary::GetNumberOfDocuments() const
{
  return documents_.size();
}

bool parsePostingBlobHeader(const uint8_t* begin, const uint8_t* end, PostingBlobHeader* out,
                            const uint8_t** fieldStreamsBegin)
{
  if (!begin || !end || !out || !fieldStreamsBegin)
    return false;
  if (begin + 56 > end)
    return false;

  out->documentFrequency = read_u64_le(begin);
  out->normalCollectionFrequency = read_u64_le(begin + 8);
  out->titleCollectionFrequency = read_u64_le(begin + 16);
  out->anchorCollectionFrequency = read_u64_le(begin + 24);
  out->normalSectionCount = read_u64_le(begin + 32);
  out->titleSectionCount = read_u64_le(begin + 40);
  out->anchorSectionCount = read_u64_le(begin + 48);
  *fieldStreamsBegin = begin + 56;
  return true;
}

bool IndexFieldStreamReader::init(const uint8_t* begin, const uint8_t* end, uint64_t sectionCount,
                                  uint64_t positionCount, FieldStreamEncoding encoding,
                                  IndexFieldStreamReader* out)
{
  if (!begin || !end || !out || begin > end)
    return false;

  out->cursor_ = begin;
  out->end_ = end;
  out->remainingPositions_ = positionCount;
  out->currentPosition_ = 0;
  out->decorators_.clear();

  const uint8_t* cursor = begin;
  const uint8_t* next = nullptr;

  if (encoding == FieldStreamEncoding::V2_SKIP_POINTERS)
  {
    for (uint64_t i = 0; i < sectionCount; i++)
    {
      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      (void)read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;
    }
  }
  else
  {
    for (uint64_t i = 0; i < sectionCount; i++)
    {
      const uint32_t type = read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      const uint32_t payloadBytes = read_var_u32(cursor, end, &next);
      if (!next)
        return false;
      cursor = next;

      if (!skip_bytes(&cursor, end, payloadBytes))
        return false;

      out->decorators_.pushBack({type, payloadBytes});
    }
  }

  out->cursor_ = cursor;
  return true;
}

bool IndexFieldStreamReader::nextPosition(uint32_t* absolutePosition)
{
  if (!absolutePosition || remainingPositions_ == 0)
    return false;

  const uint8_t* next = nullptr;
  const uint32_t delta = read_var_u32(cursor_, end_, &next);
  if (!next)
    return false;

  cursor_ = next;
  currentPosition_ += delta;
  remainingPositions_--;
  *absolutePosition = currentPosition_;
  return true;
}

const uint8_t* IndexFieldStreamReader::nextCursor() const
{
  return cursor_;
}

uint64_t IndexFieldStreamReader::remainingPositions() const
{
  return remainingPositions_;
}

const avengine::vector<FieldDecorator>& IndexFieldStreamReader::decorators() const
{
  return decorators_;
}

} // namespace avengine
