#include <avengine/crawler/url_normalizer.h>
#include <avengine/html_parser/HtmlParser.h>
#include <avengine/indexer/indexer.h>
#include <avengine/string.h>
#include <avengine/vector.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace
{

namespace fs = std::filesystem;

struct Args
{
  fs::path fixturesDir;
  fs::path outputPath;
  size_t repetitions = 50;
};

struct FixtureDocument
{
  avengine::string name;
  avengine::string url;
  avengine::string html;
};

struct Metrics
{
  size_t documentCount = 0;
  size_t totalBytes = 0;
  size_t totalWords = 0;
  size_t normalizedLinkCount = 0;
  size_t shardCount = 0;
  long long parseIndexMs = 0;
  long long serializeMs = 0;
  long long totalMs = 0;
};

avengine::string JsonEscape(const avengine::string& value)
{
  avengine::string out;
  for (char ch : value)
  {
    switch (ch)
    {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.pushBack(ch);
      break;
    }
  }
  return out;
}

bool StartsWith(const char* arg, const char* prefix)
{
  while (*prefix != '\0')
  {
    if (*arg++ != *prefix++)
      return false;
  }
  return true;
}

Args ParseArgs(int argc, char** argv)
{
  Args args;
  for (int i = 1; i < argc; ++i)
  {
    const avengine::string arg(argv[i]);
    if (arg == "--fixtures" && i + 1 < argc)
    {
      args.fixturesDir = fs::path(argv[++i]);
      continue;
    }
    if (arg == "--output" && i + 1 < argc)
    {
      args.outputPath = fs::path(argv[++i]);
      continue;
    }
    if (arg == "--repetitions" && i + 1 < argc)
    {
      args.repetitions = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
      continue;
    }
    if (StartsWith(argv[i], "--"))
    {
      std::cerr << "Unknown option: " << arg << "\n";
      std::exit(2);
    }
  }

  if (args.fixturesDir.empty() || args.outputPath.empty() || args.repetitions == 0)
  {
    std::cerr << "Usage: crawler_index_profile --fixtures <dir> --output <file> "
                 "[--repetitions <n>]\n";
    std::exit(2);
  }
  return args;
}

avengine::string ReadFile(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    std::cerr << "Failed to open fixture " << path << "\n";
    std::exit(1);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string contents = buffer.str();
  return avengine::string(contents.c_str());
}

avengine::string MakeFixtureUrl(const fs::path& path)
{
  avengine::string stem(path.stem().string().c_str());
  for (size_t i = 0; i < stem.size(); ++i)
  {
    char& ch = stem[i];
    if (!std::isalnum(static_cast<unsigned char>(ch)))
      ch = '-';
  }
  avengine::string url("https://profile.avengine.test/");
  url += stem;
  return url;
}

avengine::vector<FixtureDocument> LoadFixtures(const fs::path& fixturesDir)
{
  avengine::vector<FixtureDocument> fixtures;
  for (const fs::directory_entry& entry : fs::directory_iterator(fixturesDir))
  {
    if (!entry.is_regular_file() || entry.path().extension() != ".html")
      continue;

    FixtureDocument doc;
    doc.name = avengine::string(entry.path().filename().string().c_str());
    doc.url = MakeFixtureUrl(entry.path());
    doc.html = ReadFile(entry.path());
    fixtures.pushBack(doc);
  }

  std::sort(fixtures.begin(), fixtures.end(),
            [](const FixtureDocument& lhs, const FixtureDocument& rhs)
            { return lhs.name < rhs.name; });

  if (fixtures.empty())
  {
    std::cerr << "No .html fixtures found in " << fixturesDir << "\n";
    std::exit(1);
  }
  return fixtures;
}

fs::path MakeTempIndexDir()
{
  fs::path base = fs::temp_directory_path() / "avengine_crawler_profile_XXXXXX";
  const avengine::string pattern(base.string().c_str());
  avengine::vector<char> writable;
  writable.reserve(pattern.size() + 1);
  for (char ch : pattern)
    writable.pushBack(ch);
  writable.pushBack('\0');
  char* created = mkdtemp(writable.begin());
  if (!created)
  {
    std::perror("mkdtemp");
    std::exit(1);
  }
  return fs::path(created);
}

size_t CountShards(const fs::path& directory)
{
  size_t count = 0;
  for (const fs::directory_entry& entry : fs::directory_iterator(directory))
  {
    if (entry.is_regular_file() && entry.path().extension() == ".sfi")
      ++count;
  }
  return count;
}

void RemoveTempDir(const fs::path& directory)
{
  std::error_code ec;
  fs::remove_all(directory, ec);
}

Metrics RunProfile(const Args& args, const avengine::vector<FixtureDocument>& fixtures)
{
  Metrics metrics;
  metrics.documentCount = fixtures.size() * args.repetitions;

  fs::path tempDir = MakeTempIndexDir();
  const fs::path originalDir = fs::current_path();

  if (chdir(tempDir.c_str()) != 0)
  {
    std::perror("chdir");
    std::exit(1);
  }

  resumeIndexer();
  avengine::crawler::URLNormalizer normalizer;

  const auto totalStart = std::chrono::steady_clock::now();
  const auto parseIndexStart = totalStart;

  for (size_t repetition = 0; repetition < args.repetitions; ++repetition)
  {
    for (const FixtureDocument& fixture : fixtures)
    {
      metrics.totalBytes += fixture.html.size();

      avengine::HtmlParser parser(fixture.html.cstr(), fixture.html.size());
      if (parser.rejected)
      {
        std::cerr << "Fixture rejected by parser: " << fixture.name << "\n";
        std::exit(1);
      }

      DocumentMetadata metadata = {
          .url = fixture.url,
          .title = parser.fullTitle,
          .description = parser.rawDescription,
          .input = 0,
      };
      metrics.totalWords += indexDocument(metadata, parser.words);

      for (size_t i = 0; i < parser.links.size(); ++i)
      {
        const avengine::string raw = parser.links[i].URL.toString();
        const avengine::string normalized = normalizer.normalize(metadata.url, raw);
        if (normalized.size() != 0)
          ++metrics.normalizedLinkCount;
      }
    }
  }

  const auto parseIndexEnd = std::chrono::steady_clock::now();
  metrics.parseIndexMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(parseIndexEnd - parseIndexStart)
          .count();

  const auto serializeStart = std::chrono::steady_clock::now();
  cleanUpIndexer();
  const auto serializeEnd = std::chrono::steady_clock::now();
  metrics.serializeMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(serializeEnd - serializeStart).count();
  metrics.totalMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(serializeEnd - totalStart).count();

  metrics.shardCount = CountShards(tempDir);

  if (chdir(originalDir.c_str()) != 0)
  {
    std::perror("chdir");
    std::exit(1);
  }

  RemoveTempDir(tempDir);
  return metrics;
}

void WriteJson(const Args& args, const Metrics& metrics)
{
  fs::create_directories(args.outputPath.parent_path());
  std::ofstream out(args.outputPath);
  if (!out)
  {
    std::cerr << "Failed to open output file " << args.outputPath << "\n";
    std::exit(1);
  }

  const double docsPerSecond =
      metrics.totalMs > 0 ? (static_cast<double>(metrics.documentCount) * 1000.0) / metrics.totalMs
                          : 0.0;
  const double megabytesPerSecond =
      metrics.totalMs > 0
          ? (static_cast<double>(metrics.totalBytes) / (1024.0 * 1024.0)) * 1000.0 / metrics.totalMs
          : 0.0;

  out << std::fixed << std::setprecision(3);
  out << "{\n";
  out << "  \"profile\": \"crawler_index\",\n";
  out << "  \"fixtures_dir\": \"" << JsonEscape(avengine::string(args.fixturesDir.string().c_str()))
      << "\",\n";
  out << "  \"repetitions\": " << args.repetitions << ",\n";
  out << "  \"document_count\": " << metrics.documentCount << ",\n";
  out << "  \"total_bytes\": " << metrics.totalBytes << ",\n";
  out << "  \"total_words\": " << metrics.totalWords << ",\n";
  out << "  \"normalized_link_count\": " << metrics.normalizedLinkCount << ",\n";
  out << "  \"shard_count\": " << metrics.shardCount << ",\n";
  out << "  \"parse_index_ms\": " << metrics.parseIndexMs << ",\n";
  out << "  \"serialize_ms\": " << metrics.serializeMs << ",\n";
  out << "  \"total_ms\": " << metrics.totalMs << ",\n";
  out << "  \"docs_per_sec\": " << docsPerSecond << ",\n";
  out << "  \"mb_per_sec\": " << megabytesPerSecond << "\n";
  out << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
  const Args args = ParseArgs(argc, argv);
  const avengine::vector<FixtureDocument> fixtures = LoadFixtures(args.fixturesDir);
  const Metrics metrics = RunProfile(args, fixtures);
  WriteJson(args, metrics);

  std::cout << "crawler_index_profile wrote " << args.outputPath << "\n";
  return 0;
}
