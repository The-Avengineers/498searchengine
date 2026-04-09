// tune.cpp — offline ranker weight tuner
//
// Usage: ./tune [weights.cfg] [log_file]
//   weights.cfg  default: weights.cfg
//   log_file     default: search.log
// Run this on demand not part of the search server.

#include <avengine/gradient_descent/search_logger.h>
#include <avengine/gradient_descent/weight_tuner.h>

#include <cstdio>
#include <cstring>

using namespace avengine;
using namespace avengine::gradient_descent;

// Minimum number of training samples before the tuner is trusted
// bc of overfitting
static constexpr size_t kMinSamples = 100;

int main(int argc, char* argv[])
{
  const char* weights_path = "weights.cfg";
  const char* log_file     = "search.log";

  if (argc >= 2) weights_path = argv[1];
  if (argc >= 3) log_file     = argv[2];

  // Load current weights.
  vector<StrategyWeight> strategy_weights = loadWeights(string(weights_path));

  if (strategy_weights.size() == 0)
  {
    fprintf(stderr, "No weights loaded from %s — create a weights.cfg first.\n", weights_path);
    return 1;
  }

  vector<double> initial_weights(strategy_weights.size(), 0.0);
  for (size_t i = 0; i < strategy_weights.size(); ++i)
    initial_weights[i] = strategy_weights[i].weight;

  // Build training data from the single append-only log file.
  TrainingDataBuilder builder;
  vector<TrainingSample> data = builder.build(string(log_file));

  printf("Training samples found: %zu\n", data.size());

  if (data.size() < kMinSamples)
  {
    printf("Not enough data (%zu samples, need at least %zu) — hand-tune weights for now.\n",
           data.size(), kMinSamples);
    return 1;
  }

  // Run tuner.
  WeightTuner tuner(initial_weights);
  vector<double> new_weights = tuner.train(data);

  // Print comparison of new vs old weights
  printf("\n%-22s %10s %10s %12s\n", "Strategy", "Old", "New", "Delta");
  printf("------------------------------------------------------\n");

  bool any_change = false;
  for (size_t i = 0; i < strategy_weights.size(); ++i)
  {
    double old_w = initial_weights[i];
    double new_w = new_weights[i];
    double delta = new_w - old_w;
    if (delta != 0.0) any_change = true;

    printf("%-22s %10.4f %10.4f %12.4f\n",
           strategy_weights[i].name.cstr(), old_w, new_w, delta);
  }
  printf("\n");

  if (!any_change)
  {
    printf("No weight changes — current weights already minimise loss on this data.\n");
    return 0;
  }

  // Write new weights back.
  for (size_t i = 0; i < strategy_weights.size(); ++i)
    strategy_weights[i].weight = new_weights[i];

  saveWeights(string(weights_path), strategy_weights);
  printf("Weights written to %s\n", weights_path);
  printf("Restart the search server to apply.\n");

  return 0;
}
