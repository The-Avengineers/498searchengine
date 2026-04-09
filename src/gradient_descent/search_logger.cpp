#include <avengine/gradient_descent/search_logger.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace avengine
{
namespace gradient_descent
{

namespace
{

// Static counter is shared across threads and protected by a static mutex,
// tried to make siilar as cralwer logger

string generateSessionId()
{
  static mutex mu;
  static size_t counter = 0;
  lock_guard<mutex> guard(mu);
  long ts = static_cast<long>(time(nullptr));
  char buf[64];
  snprintf(buf, sizeof(buf), "%ld_%zu", ts, counter++);
  return string(buf);
}

}

SearchLogger::SearchLogger(const string& log_file_path)
  : log_file_(fopen(log_file_path.cstr(), "a"))
{
}

SearchLogger::~SearchLogger()
{
  if (log_file_)
    fclose(log_file_);
}

string SearchLogger::logSearch(const string& query,
                               const vector<Ranker::RankedResult>& results)
{
  SearchLog log;
  log.session_id = generateSessionId();
  log.query = query;
  log.clicked_index = -1;
  log.timestamp = static_cast<long>(time(nullptr));

  for (size_t i = 0; i < results.size(); ++i)
  {
    log.urls_shown.pushBack(results[i].entry.url);
    log.final_scores.pushBack(results[i].score);
    log.feature_scores.pushBack(vector<double>()); 
  }

  lock_guard<mutex> guard(mu_);
  sessions_.pushBack(log);
  return log.session_id;
}

void SearchLogger::logClick(const string& session_id, int clicked_index)
{
  lock_guard<mutex> guard(mu_);

  for (size_t i = 0; i < sessions_.size(); ++i)
  {
    if (sessions_[i].session_id == session_id)
    {
      sessions_[i].clicked_index = clicked_index;
      writeSample(sessions_[i]);
      sessions_[i] = sessions_[sessions_.size() - 1];
      sessions_.popBack();
      return;
    }
  }
}

// session_id\ttimestamp\tquery\tclicked_index\tdoc_count\turl\tfinal_score\tfeat_count\tf0 f1...\t...
void SearchLogger::writeSample(const SearchLog& log) const
{
  if (!log_file_) return;
  if (log.clicked_index < 0 ||
      static_cast<size_t>(log.clicked_index) >= log.urls_shown.size())
    return;

  // Only write up to and including the clicked result —
  // docs ranked below the click are never used by the tuner.
  size_t docs_to_write = static_cast<size_t>(log.clicked_index + 1);

  fprintf(log_file_, "%s\t%ld\t%s\t%d\t%zu",
          log.session_id.cstr(), log.timestamp,
          log.query.cstr(), log.clicked_index,
          docs_to_write);

  for (size_t i = 0; i < docs_to_write; ++i)
  {
    const vector<double>& feats = log.feature_scores[i];
    fprintf(log_file_, "\t%s\t%.15g\t%zu\t",
            log.urls_shown[i].cstr(), log.final_scores[i], feats.size());
    for (size_t j = 0; j < feats.size(); ++j)
    {
      if (j > 0) fputc(' ', log_file_);
      fprintf(log_file_, "%.15g", feats[j]);
    }
  }

  fputc('\n', log_file_);
  fflush(log_file_);
}

vector<TrainingSample> TrainingDataBuilder::build(const string& log_file_path) const
{
  vector<TrainingSample> samples;

  FILE* f = fopen(log_file_path.cstr(), "r");
  if (!f) return samples;

  
  char buf[65536];
  while (fgets(buf, sizeof(buf), f))
  {
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    vector<string> fields = string(buf).split('\t');

    // Minimum: session_id, timestamp, query, clicked_index, doc_count = 5 fields.
    if (fields.size() < 5) continue;

    int clicked_index = static_cast<int>(strtol(fields[3].cstr(), nullptr, 10));
    if (clicked_index <= 0) continue; // no click or top result clicked — no negatives

    int doc_count = static_cast<int>(strtol(fields[4].cstr(), nullptr, 10));

    // Parse per-doc feature vectors. Each doc: url, final_score, feat_count, features = 4 fields.
    vector<vector<double>> all_features;
    size_t idx = 5;
    for (int d = 0; d < doc_count && idx + 3 < fields.size(); ++d, idx += 4)
    {
      int feat_count = static_cast<int>(strtol(fields[idx + 2].cstr(), nullptr, 10));
      vector<double> feats;
      const char* p = fields[idx + 3].cstr();
      for (int fi = 0; fi < feat_count; ++fi)
      {
        char* end;
        double v = strtod(p, &end);
        if (end == p) break;
        feats.pushBack(v);
        p = end;
      }
      all_features.pushBack(feats);
    }

    if (clicked_index >= static_cast<int>(all_features.size())) continue;

    TrainingSample sample;
    sample.positive_features = all_features[clicked_index];
    for (int j = 0; j < clicked_index; ++j)
      sample.negative_features.pushBack(all_features[j]);

    samples.pushBack(sample);
  }

  fclose(f);
  return samples;
}

} // namespace gradient_descent
} // namespace avengine
