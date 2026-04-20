#include <avengine/search/serving_shard_source.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace avengine
{
namespace search
{
namespace
{

bool RangeInFile(uint64_t offset, uint64_t length, size_t fileSize)
{
  return offset <= fileSize && length <= static_cast<uint64_t>(fileSize) - offset;
}

bool ReadVarU32(const uint8_t* end, const uint8_t** cursor, uint32_t* out)
{
  if (!cursor || !out || *cursor > end)
    return false;

  uint32_t value = 0;
  uint32_t shift = 0;
  const uint8_t* p = *cursor;
  while (p < end && shift < 35)
  {
    const uint8_t byte = *p++;
    value |= static_cast<uint32_t>(byte & 0x7fu) << shift;
    if ((byte & 0x80u) == 0)
    {
      *cursor = p;
      *out = value;
      return true;
    }
    shift += 7u;
  }
  return false;
}

} // namespace

bool ServingPostingBlockCursor::IsValid() const
{
  return valid_;
}

void ServingPostingBlockCursor::Reset(const uint8_t* payload, uint64_t payloadLength,
                                      uint32_t docCount, uint32_t tierId)
{
  cursor_ = payload;
  end_ = payload ? payload + payloadLength : nullptr;
  remaining_ = docCount;
  decoded_ = 0;
  previousDocId_ = 0;
  tierId_ = tierId;
  valid_ = payload != nullptr;
}

bool ServingPostingBlockCursor::Next(ServingDecodedPosting* out)
{
  if (!valid_ || !out || remaining_ == 0)
    return false;

  uint32_t delta = 0;
  if (!ReadVarU32(end_, &cursor_, &delta) || cursor_ >= end_)
  {
    valid_ = false;
    return false;
  }

  ServingDecodedPosting posting = {};
  posting.docId = decoded_ == 0 ? delta : previousDocId_ + delta;
  previousDocId_ = posting.docId;
  posting.tierId = tierId_;

  const uint8_t mask = *cursor_++;
  if ((mask & kServingFieldBody) != 0 && !ReadVarU32(end_, &cursor_, &posting.bodyTf))
  {
    valid_ = false;
    return false;
  }
  if ((mask & kServingFieldTitle) != 0 && !ReadVarU32(end_, &cursor_, &posting.titleTf))
  {
    valid_ = false;
    return false;
  }
  if ((mask & kServingFieldAnchor) != 0 && !ReadVarU32(end_, &cursor_, &posting.anchorTf))
  {
    valid_ = false;
    return false;
  }
  if (cursor_ > end_)
  {
    valid_ = false;
    return false;
  }

  ++decoded_;
  --remaining_;
  if (remaining_ == 0 && cursor_ != end_)
  {
    valid_ = false;
    return false;
  }

  *out = posting;
  return true;
}

ServingShardSource::ServingShardSource(const string& path)
{
  Load(path);
}

ServingShardSource::~ServingShardSource()
{
  Reset();
}

bool ServingShardSource::Load(const string& path)
{
  Reset();
  path_ = path;

  const int fd = open(path.cstr(), O_RDONLY);
  if (fd < 0)
  {
    SetErrnoError("open", path);
    return false;
  }

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(ServingShardHeaderV1)))
  {
    SetErrnoError("fstat", path);
    close(fd);
    return false;
  }

  fileSize_ = static_cast<size_t>(st.st_size);
  storage_.resize(fileSize_);
  size_t total = 0;
  while (total < fileSize_)
  {
    const ssize_t n = read(fd, storage_.begin() + total, fileSize_ - total);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      SetErrnoError("read", path);
      close(fd);
      Reset();
      return false;
    }
    if (n == 0)
    {
      SetError("truncated serving shard read");
      close(fd);
      Reset();
      return false;
    }
    total += static_cast<size_t>(n);
  }
  close(fd);
  data_ = storage_.begin();

  if (!Validate())
  {
    Reset();
    return false;
  }

  loaded_ = true;
  return true;
}

void ServingShardSource::Reset()
{
  vector<uint8_t> empty;
  storage_.swap(empty);
  data_ = nullptr;
  fileSize_ = 0;
  loaded_ = false;
  header_ = nullptr;
  lexicon_ = nullptr;
  termBytes_ = nullptr;
  termInfos_ = nullptr;
  tierInfos_ = nullptr;
  blockInfos_ = nullptr;
  postings_ = nullptr;
  docLengths_ = nullptr;
  rankMetadata_ = nullptr;
  coldDocs_ = nullptr;
  positionHeader_ = nullptr;
  termPositions_ = nullptr;
  docPositions_ = nullptr;
  positionPayload_ = nullptr;
}

bool ServingShardSource::IsLoaded() const
{
  return loaded_;
}

string ServingShardSource::GetError() const
{
  return error_;
}

size_t ServingShardSource::DocumentCount() const
{
  return header_ ? static_cast<size_t>(header_->documentCount) : 0u;
}

size_t ServingShardSource::TermCount() const
{
  return header_ ? static_cast<size_t>(header_->termCount) : 0u;
}

bool ServingShardSource::LookupTerm(const string& term, ServingTermView* out) const
{
  if (out)
    *out = ServingTermView();
  if (!loaded_ || !out)
    return false;

  const ServingLexiconEntryV1* lex = FindLexiconEntry(term);
  if (!lex || lex->termInfoIndex >= header_->termCount)
    return false;

  const ServingTermInfoV1& termInfo = termInfos_[lex->termInfoIndex];
  if (termInfo.tierInfoIndex + termInfo.tierCount > header_->tierCount)
    return false;

  out->termInfo = &termInfo;
  out->tiers = tierInfos_ + termInfo.tierInfoIndex;
  out->tierCount = termInfo.tierCount;
  return true;
}

const ServingBlockInfoV1* ServingShardSource::GetBlockInfo(const ServingTierInfoV1& tier,
                                                           uint32_t blockOffset) const
{
  if (!loaded_ || blockOffset >= tier.blockCount ||
      tier.blockIndex + blockOffset >= header_->blockCount)
  {
    return nullptr;
  }
  return blockInfos_ + tier.blockIndex + blockOffset;
}

bool ServingShardSource::OpenBlockCursor(const ServingBlockInfoV1& block, uint32_t tierId,
                                         ServingPostingBlockCursor* out) const
{
  if (out)
    *out = ServingPostingBlockCursor();
  if (!loaded_ || !out || block.payloadOffset + block.payloadLength > header_->postingsLength)
    return false;

  out->Reset(postings_ + block.payloadOffset, block.payloadLength, block.docCount, tierId);
  return out->IsValid();
}

bool ServingShardSource::DecodeTermPostings(const string& term,
                                            vector<ServingDecodedPosting>* out) const
{
  if (!out)
    return false;
  out->clear();

  ServingTermView view;
  if (!LookupTerm(term, &view))
    return false;

  for (uint32_t ti = 0; ti < view.tierCount; ++ti)
  {
    const ServingTierInfoV1& tier = view.tiers[ti];
    if (tier.blockIndex + tier.blockCount > header_->blockCount)
      return false;

    for (uint32_t bi = 0; bi < tier.blockCount; ++bi)
    {
      const ServingBlockInfoV1* block = GetBlockInfo(tier, bi);
      if (!block)
        return false;

      ServingPostingBlockCursor cursor;
      if (!OpenBlockCursor(*block, tier.tierId, &cursor))
        return false;

      ServingDecodedPosting posting;
      while (cursor.Next(&posting))
      {
        out->pushBack(posting);
      }
      if (!cursor.IsValid())
        return false;
    }
  }

  return true;
}

bool ServingShardSource::DecodePositionsFromEntry(const ServingDocPositionEntryV1& entry,
                                                  vector<size_t>* out) const
{
  if (!out || !positionPayload_ || entry.payloadOffset + entry.payloadLength >
                                   positionHeader_->payloadLength)
  {
    return false;
  }

  out->clear();
  const uint8_t* cursor = positionPayload_ + entry.payloadOffset;
  const uint8_t* end = cursor + entry.payloadLength;
  uint32_t previous = 0;
  for (uint32_t i = 0; i < entry.positionCount; ++i)
  {
    uint32_t delta = 0;
    if (!ReadVarU32(end, &cursor, &delta))
      return false;
    const uint32_t position = i == 0 ? delta : previous + delta;
    previous = position;
    out->pushBack(position);
  }

  return cursor == end;
}

bool ServingShardSource::DecodeTermPositions(const string& term, uint32_t docId,
                                             ServingPositionField field,
                                             vector<size_t>* out) const
{
  if (out)
    out->clear();
  if (!loaded_ || !out || !HasPositionData())
    return false;

  const ServingLexiconEntryV1* lex = FindLexiconEntry(term);
  if (!lex || lex->termInfoIndex >= header_->termCount)
    return false;

  const ServingTermPositionInfoV1& termPosition = termPositions_[lex->termInfoIndex];
  const uint32_t wantedField = static_cast<uint32_t>(field);
  size_t left = termPosition.docPositionIndex;
  size_t right = left + termPosition.docPositionCount;
  if (right > positionHeader_->docPositionEntryCount)
    return false;

  while (left < right)
  {
    const size_t mid = left + ((right - left) / 2u);
    const ServingDocPositionEntryV1& entry = docPositions_[mid];
    if (entry.docId < docId || (entry.docId == docId && entry.fieldId < wantedField))
      left = mid + 1u;
    else
      right = mid;
  }

  if (left >= termPosition.docPositionIndex + termPosition.docPositionCount)
    return true;

  const ServingDocPositionEntryV1& found = docPositions_[left];
  if (found.docId != docId || found.fieldId != wantedField)
    return true;

  return DecodePositionsFromEntry(found, out);
}

bool ServingShardSource::HasPositionData() const
{
  return loaded_ && positionHeader_ && termPositions_ && docPositions_ && positionPayload_;
}

uint32_t ServingShardSource::GetDocLength(uint32_t docId) const
{
  if (!loaded_ || !docLengths_ || static_cast<uint64_t>(docId) >= header_->documentCount)
    return 0;
  return docLengths_[docId];
}

ServingDocRankMetadataV1 ServingShardSource::GetDocumentRankMetadata(uint32_t docId) const
{
  if (!loaded_ || !rankMetadata_ || static_cast<uint64_t>(docId) >= header_->documentCount)
    return ServingDocRankMetadataV1();
  return rankMetadata_[docId];
}

string ServingShardSource::GetDocumentURL(uint32_t docId) const
{
  if (!loaded_ || !coldDocs_ || static_cast<uint64_t>(docId) >= header_->documentCount)
    return string();
  return ReadColdString(coldDocs_[docId].urlOffset, coldDocs_[docId].urlLength);
}

string ServingShardSource::GetDocumentTitle(uint32_t docId) const
{
  if (!loaded_ || !coldDocs_ || static_cast<uint64_t>(docId) >= header_->documentCount)
    return string();
  return ReadColdString(coldDocs_[docId].titleOffset, coldDocs_[docId].titleLength);
}

string ServingShardSource::GetDocumentDescription(uint32_t docId) const
{
  if (!loaded_ || !coldDocs_ || static_cast<uint64_t>(docId) >= header_->documentCount)
    return string();
  return ReadColdString(coldDocs_[docId].descriptionOffset, coldDocs_[docId].descriptionLength);
}

bool ServingShardSource::Validate()
{
  header_ = reinterpret_cast<const ServingShardHeaderV1*>(data_);
  if (!IsServingShardMagic(header_->magic))
  {
    SetError("invalid serving shard magic");
    return false;
  }
  if (!IsSupportedServingShardVersion(header_->version) ||
      header_->headerBytes != sizeof(ServingShardHeaderV1))
  {
    SetError("unsupported serving shard version");
    return false;
  }

  if (!RangeInFile(header_->lexiconOffset, header_->lexiconLength, fileSize_) ||
      !RangeInFile(header_->termBytesOffset, header_->termBytesLength, fileSize_) ||
      !RangeInFile(header_->termInfoOffset, header_->termInfoLength, fileSize_) ||
      !RangeInFile(header_->tierInfoOffset, header_->tierInfoLength, fileSize_) ||
      !RangeInFile(header_->blockInfoOffset, header_->blockInfoLength, fileSize_) ||
      !RangeInFile(header_->postingsOffset, header_->postingsLength, fileSize_) ||
      !RangeInFile(header_->docLengthOffset, header_->docLengthLength, fileSize_) ||
      !RangeInFile(header_->staticRankOffset, header_->staticRankLength, fileSize_) ||
      !RangeInFile(header_->coldPayloadOffset, header_->coldPayloadLength, fileSize_) ||
      !RangeInFile(header_->debugOffset, header_->debugLength, fileSize_))
  {
    SetError("serving shard section out of bounds");
    return false;
  }

  if (header_->lexiconLength != header_->lexiconEntryCount * sizeof(ServingLexiconEntryV1) ||
      header_->termInfoLength != header_->termCount * sizeof(ServingTermInfoV1) ||
      header_->tierInfoLength != header_->tierCount * sizeof(ServingTierInfoV1) ||
      header_->blockInfoLength != header_->blockCount * sizeof(ServingBlockInfoV1) ||
      header_->docLengthLength != header_->documentCount * sizeof(uint32_t))
  {
    SetError("serving shard section size mismatch");
    return false;
  }

  if ((header_->flags & kServingShardHasColdPayload) != 0 &&
      header_->coldPayloadLength < header_->documentCount * sizeof(ServingDocMetadataEntryV1))
  {
    SetError("serving shard cold payload too small");
    return false;
  }
  if ((header_->flags & kServingShardHasRankMetadata) != 0 &&
      header_->staticRankLength != header_->documentCount * sizeof(ServingDocRankMetadataV1))
  {
    SetError("serving shard rank metadata size mismatch");
    return false;
  }

  lexicon_ = reinterpret_cast<const ServingLexiconEntryV1*>(data_ + header_->lexiconOffset);
  termBytes_ = data_ + header_->termBytesOffset;
  termInfos_ = reinterpret_cast<const ServingTermInfoV1*>(data_ + header_->termInfoOffset);
  tierInfos_ = reinterpret_cast<const ServingTierInfoV1*>(data_ + header_->tierInfoOffset);
  blockInfos_ = reinterpret_cast<const ServingBlockInfoV1*>(data_ + header_->blockInfoOffset);
  postings_ = data_ + header_->postingsOffset;
  docLengths_ = reinterpret_cast<const uint32_t*>(data_ + header_->docLengthOffset);
  rankMetadata_ = (header_->flags & kServingShardHasRankMetadata) != 0
      ? reinterpret_cast<const ServingDocRankMetadataV1*>(data_ + header_->staticRankOffset)
      : nullptr;
  coldDocs_ = (header_->flags & kServingShardHasColdPayload) != 0
      ? reinterpret_cast<const ServingDocMetadataEntryV1*>(data_ + header_->coldPayloadOffset)
      : nullptr;

  if ((header_->flags & kServingShardHasPositions) != 0)
  {
    if (header_->debugLength < sizeof(ServingPositionSectionHeaderV1))
    {
      SetError("serving shard position section too small");
      return false;
    }

    const uint8_t* positionBase = data_ + header_->debugOffset;
    positionHeader_ = reinterpret_cast<const ServingPositionSectionHeaderV1*>(positionBase);
    if (positionHeader_->magic != kServingPositionSectionMagic ||
        positionHeader_->version != 1 ||
        positionHeader_->termCount != header_->termCount)
    {
      SetError("invalid serving shard position section");
      return false;
    }

    if (!RangeInFile(positionHeader_->termPositionOffset, positionHeader_->termPositionLength,
                     static_cast<size_t>(header_->debugLength)) ||
        !RangeInFile(positionHeader_->docPositionOffset, positionHeader_->docPositionLength,
                     static_cast<size_t>(header_->debugLength)) ||
        !RangeInFile(positionHeader_->payloadOffset, positionHeader_->payloadLength,
                     static_cast<size_t>(header_->debugLength)) ||
        positionHeader_->termPositionLength !=
            positionHeader_->termCount * sizeof(ServingTermPositionInfoV1) ||
        positionHeader_->docPositionLength !=
            positionHeader_->docPositionEntryCount * sizeof(ServingDocPositionEntryV1))
    {
      SetError("serving shard position section bounds invalid");
      return false;
    }

    termPositions_ = reinterpret_cast<const ServingTermPositionInfoV1*>(
        positionBase + positionHeader_->termPositionOffset);
    docPositions_ = reinterpret_cast<const ServingDocPositionEntryV1*>(
        positionBase + positionHeader_->docPositionOffset);
    positionPayload_ = positionBase + positionHeader_->payloadOffset;
  }

  return true;
}

const ServingLexiconEntryV1* ServingShardSource::FindLexiconEntry(const string& term) const
{
  size_t left = 0;
  size_t right = static_cast<size_t>(header_->lexiconEntryCount);
  while (left < right)
  {
    const size_t mid = left + ((right - left) / 2u);
    const ServingLexiconEntryV1& entry = lexicon_[mid];
    const char* midBytes = reinterpret_cast<const char*>(termBytes_ + entry.termOffset);
    const size_t minLen = term.size() < entry.termLength ? term.size() : entry.termLength;
    int cmp = std::memcmp(midBytes, term.cstr(), minLen);
    if (cmp == 0)
    {
      if (entry.termLength < term.size())
        cmp = -1;
      else if (entry.termLength > term.size())
        cmp = 1;
    }

    if (cmp < 0)
      left = mid + 1u;
    else if (cmp > 0)
      right = mid;
    else
      return &entry;
  }
  return nullptr;
}

string ServingShardSource::ReadColdString(uint32_t offset, uint32_t length) const
{
  if (!RangeInFile(header_->coldPayloadOffset + offset, length, fileSize_))
    return string();
  string out;
  const char* bytes = reinterpret_cast<const char*>(data_ + header_->coldPayloadOffset + offset);
  for (uint32_t i = 0; i < length; ++i)
    out.pushBack(bytes[i]);
  return out;
}

void ServingShardSource::SetError(const char* message)
{
  error_ = string(message ? message : "serving shard source error");
}

void ServingShardSource::SetErrnoError(const char* operation, const string& path)
{
  error_ = string(operation ? operation : "operation");
  error_ += string(": ");
  error_ += path;
  error_ += string(": ");
  error_ += string(std::strerror(errno));
}

} // namespace search
} // namespace avengine
