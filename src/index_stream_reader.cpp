#include "avengine/index_stream_reader.h"

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

static size_t lower_bound_index(const vector<Location>& values, Location target)
{
  size_t left = 0;
  size_t right = values.size();

  while (left < right)
  {
    const size_t mid = left + ((right - left) / 2);
    if (values[mid] < target)
      left = mid + 1;
    else
      right = mid;
  }

  return left;
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

  const size_t foundIndex = lower_bound_index(occurrences_, target);
  if (foundIndex >= occurrences_.size())
  {
    currentIndex_ = occurrences_.size();
    exhausted_ = true;
    ClearCurrent();
    return nullptr;
  }

  return SetCurrentFromIndex(foundIndex);
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

Dictionary::Dictionary() : termLocations_(strings_equal, hash_string, 256) {}

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

  const string key(token);
  Tuple<string, vector<Location>>* found = termLocations_.Find(key);
  if (!found)
  {
    found = termLocations_.Find(key, vector<Location>());
    uniqueWords_++;
  }

  found->value.pushBack(location);
  totalWords_++;
}

ISRWord* Dictionary::OpenIsr(const char* token) const
{
  if (!token)
    return nullptr;

  Tuple<string, vector<Location>>* found = termLocations_.Find(string(token));
  if (!found)
    return nullptr;

  uint32_t documentCount = 0;
  uint32_t lastDocumentId = static_cast<uint32_t>(-1);
  for (Location location : found->value)
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

  if (documentCount == 0 && !found->value.empty())
    documentCount = 1;

  return new ISRWord(found->value, documents_, documentCount);
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
  return uniqueWords_;
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
  if (begin + 24 > end)
    return false;

  out->documentFrequency = read_u64_le(begin);
  out->collectionFrequency = read_u64_le(begin + 8);
  out->skipCount = read_u64_le(begin + 16);
  *fieldStreamsBegin = begin + 24; // Skip [doc_freq][collection_freq][skip_count].
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
