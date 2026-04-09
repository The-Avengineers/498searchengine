#pragma once

#include <avengine/avengine_lib/lock_guard.h>
#include <avengine/avengine_lib/mutex.h>
#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/ranker/ranker.h>

#include <cstdio>

namespace avengine
{
namespace gradient_descent
{

// One search session held in memory until a click comes in.
struct SearchLog
{
  string session_id;
  string query;
  vector<string> urls_shown;             // in ranked order
  vector<vector<double>> feature_scores; // [doc_index][strategy_index]
  vector<double> final_scores;
  int clicked_index{-1};
  long timestamp{0};
};

// One sample ready for the tuner.
struct TrainingSample
{
  vector<double> positive_features;
  vector<vector<double>> negative_features;
};

// Logs clickthrough data to a single append-only file.
//
//  logSearch: inserts a SearchLog into an in-memory vector. No file write.
//   logClick:  finds the session by linear scan, writes one complete line to
//                the file, then removes it. Never reads from disk.
//  File format: one line per useful training sample, tab-delimited because | and , can be part of
//  query
//   session_id\ttimestamp\tquery\tclicked_index\tdoc_count\turl\tfinal_score\tfeat_count\tf0
//   f1...\t...

class SearchLogger
{
public:
  explicit SearchLogger(const string& log_file_path);
  ~SearchLogger();

  SearchLogger(const SearchLogger&) = delete;
  SearchLogger& operator=(const SearchLogger&) = delete;

  // Inserts session into memory. No file write.
  // Returns the session_id — send to the frontend for /click requests.
  string logSearch(const string& query, const vector<Ranker::RankedResult>& results);

  // Finds session by linear scan, writes one complete line to file, removes
  // it from memory. Never reads from disk.
  // TODO: implement HashTable delete so can use instead of vector scan delete
  void logClick(const string& session_id, int clicked_index);

private:
  FILE* log_file_;
  mutex mu_;
  vector<SearchLog> sessions_;

  void writeSample(const SearchLog& log) const;
};

// Runs offline: reads the append-only log file and builds TrainingSample
// objects for the weight_tuner. One line = one sample
class TrainingDataBuilder
{
public:
  vector<TrainingSample> build(const string& log_file_path) const;
};

} // namespace gradient_descent
} // namespace avengine
