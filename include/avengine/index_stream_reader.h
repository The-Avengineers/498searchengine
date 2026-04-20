#pragma once

#include "avengine/HashTable.h"
#include "avengine/string.h"
#include "avengine/vector.h"
#include <avengine/indexer/indexer.h>
#include <cstddef>
#include <cstdint>

namespace avengine
{

using Location = size_t;
using FileOffset = size_t;

enum class FieldStreamEncoding
{
  V2_SKIP_POINTERS,
  V3_DECORATORS,
};

struct FieldDecorator
{
  uint32_t type = 0;
  uint32_t payloadBytes = 0;
};

struct PostingBlobHeader
{
  uint64_t documentFrequency = 0;
  uint64_t collectionFrequency = 0;
  uint64_t skipCount = 0;
};

struct SkipPointerEntry
{
  uint32_t positionIndex = 0;
  uint32_t positionValue = 0;
  uint32_t byteOffset = 0;
};

struct WordAttributes
{
  uint32_t documentId = 0;
  uint32_t documentFrequency = 0;
};

struct DocumentAttributes
{
  uint32_t documentId = 0;
  uint32_t documentLength = 0;
  uint32_t titleLength = 0;
  uint32_t urlLength = 0;
};

struct Attributes
{
  enum class Kind
  {
    NONE,
    WORD,
    DOCUMENT,
  };

  Kind kind = Kind::NONE;
  WordAttributes Word = {};
  DocumentAttributes Document = {};
};

class Post
{
public:
  Post() = default;
  Post(Location start, Location end, const Attributes& attributes)
      : start_(start), end_(end), attributes_(attributes)
  {
  }

  Location GetStartLocation() const
  {
    return start_;
  }

  Location GetEndLocation() const
  {
    return end_;
  }

  Attributes GetAttributes() const
  {
    return attributes_;
  }

  void Set(Location start, Location end, const Attributes& attributes)
  {
    start_ = start;
    end_ = end;
    attributes_ = attributes;
  }

private:
  Location start_ = 0;
  Location end_ = 0;
  Attributes attributes_ = {};
};

struct DocumentInfo
{
  uint32_t documentId = 0;
  Location start = 0;
  Location end = 0;
  uint32_t titleLength = 0;
  uint32_t urlLength = 0;
  RankingInput rankingInput;
};

class ISR
{
public:
  virtual ~ISR() = default;

  virtual Post* GetCurrentPost();
  virtual Post* Next() = 0;
  virtual Post* Seek(Location target) = 0;
  virtual Location GetStartLocation() const;
  virtual Location GetEndLocation() const;
  virtual unsigned GetDocumentCount() const;

protected:
  void ClearCurrent();
  Post* SetCurrent(Location start, Location end, const Attributes& attributes);

  Post currentPost_ = {};
  bool hasCurrentPost_ = false;
};

class ISRWord : public ISR
{
public:
  ISRWord() = default;
  ISRWord(const vector<Location>& occurrences, const DocumentInfo* docs, size_t docsSize,
          uint32_t documentCount);

  Post* Next() override;
  Post* Seek(Location target) override;
  unsigned GetDocumentCount() const override;

protected:
  Post* SetCurrentFromIndex(size_t index);
  bool ResolveCurrentDocument(DocumentInfo* out) const;

  vector<Location> occurrences_;
  const DocumentInfo* documents_ = nullptr;
  size_t documentsSize_ = 0;
  uint32_t documentCount_ = 0;
  size_t currentIndex_ = 0;
  bool exhausted_ = true;
};

// Lazy ISR: navigates a delta-compressed position stream using skip pointers.
// Holds raw const pointers into IndexShardSource's owned index bytes and must
// not outlive that source.
class ISRWordLazy : public ISRWord
{
public:
  ISRWordLazy(const uint8_t* streamStart, const uint8_t* streamEnd,
              vector<SkipPointerEntry> skipPointers, uint64_t collectionFrequency,
              const DocumentInfo* docs, size_t docsSize, uint32_t documentCount);

  Post* Next() override;
  Post* Seek(Location target) override;

private:
  const uint8_t* streamStart_ = nullptr;
  const uint8_t* streamEnd_ = nullptr;
  const uint8_t* cursor_ = nullptr;
  vector<SkipPointerEntry> skipPointers_;
  uint64_t collectionFrequency_ = 0;
  uint64_t positionsDecoded_ = 0;
  uint32_t currentAbsPosition_ = 0;
};

class ISREndDoc : public ISRWord
{
public:
  ISREndDoc() = default;
  ISREndDoc(const DocumentInfo* docs, size_t count);

  unsigned GetDocumentLength() const;
  unsigned GetTitleLength() const;
  unsigned GetUrlLength() const;
  uint32_t GetCurrentDocumentId() const;
  Location GetDocumentStart() const;
};

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

class ISRContainer : public ISR
{
public:
  ISRContainer() = default;
  ISRContainer(const vector<ISR*>& contained, const vector<ISR*>& excluded, ISREndDoc* endDoc);

  Post* Seek(Location target) override;
  Post* Next() override;

  vector<ISR*> Contained;
  vector<ISR*> Excluded;
  ISREndDoc* EndDoc = nullptr;
  unsigned CountContained = 0;
  unsigned CountExcluded = 0;

private:
  unsigned nearestContained_ = 0;
  unsigned farthestContained_ = 0;
  Location nearestStartLocation_ = 0;
  Location nearestEndLocation_ = 0;
};

class Dictionary
{
public:
  Dictionary();

  void AddDocument(Location endLocation, uint32_t titleLength = 0, uint32_t urlLength = 0);
  void AddOccurrence(const char* token, Location location);

  [[nodiscard]] ISRWord* OpenIsr(const char* token) const;
  [[nodiscard]] ISREndDoc* OpenEndDocIsr() const;
  size_t GetNumberOfWords() const;
  size_t GetNumberOfUniqueWords() const;
  size_t GetNumberOfDocuments() const;

private:
  vector<DocumentInfo> documents_;
  HashTable<string, vector<Location>> termLocations_;
  size_t totalWords_ = 0;
  size_t uniqueWords_ = 0;
};

class IndexFieldStreamReader
{
public:
  static bool init(const uint8_t* begin, const uint8_t* end, uint64_t sectionCount,
                   uint64_t positionCount, FieldStreamEncoding encoding,
                   IndexFieldStreamReader* out);

  bool nextPosition(uint32_t* absolutePosition);
  const uint8_t* nextCursor() const;
  uint64_t remainingPositions() const;
  const avengine::vector<FieldDecorator>& decorators() const;

private:
  const uint8_t* cursor_ = nullptr;
  const uint8_t* end_ = nullptr;
  uint64_t remainingPositions_ = 0;
  uint32_t currentPosition_ = 0;
  avengine::vector<FieldDecorator> decorators_;
};

bool parsePostingBlobHeader(const uint8_t* begin, const uint8_t* end, PostingBlobHeader* out,
                            const uint8_t** fieldStreamsBegin);

} // namespace avengine
