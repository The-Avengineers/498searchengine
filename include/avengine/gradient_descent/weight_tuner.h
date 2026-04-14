#pragma once

#include <avengine/avengine_lib/string.h>
#include <avengine/avengine_lib/vector.h>
#include <avengine/gradient_descent/search_logger.h>

namespace avengine
{
namespace gradient_descent
{

// One entry in weights.cfg — strategy name paired with its weight.
struct StrategyWeight
{
  string name;
  double weight{1.0};
};

// Gradient descent over ranker weights using pairwise hinge loss.
//
// Loss model: for each (clicked, unclicked-above-clicked) pair, penalize when
//   dot(weights, unclicked_features) - dot(weights, clicked_features) + margin > 0
//
// Optimisation: coordinate descent — step each weight up/down and keep the
// direction that reduces total loss.  Step size shrinks by 1% per iteration
// so the search converges.
class WeightTuner
{
public:
  explicit WeightTuner(const vector<double>& initial_weights);

  double linearScore(const vector<double>& features) const;

  // Hinge loss for one (positive, negatives) sample.
  double sampleLoss(const TrainingSample& sample) const;

  // Sum of sampleLoss over all training samples.
  double totalLoss(const vector<TrainingSample>& data) const;

  // Run coordinate-descent gradient descent and return the tuned weights.
  // Weights are clamped to [0, ∞) after training
  // no negative weights and a strategy scoring high should never hurt ranking.
  vector<double> train(const vector<TrainingSample>& data);

private:
  vector<double> weights_;

  static constexpr double kMargin = 0.1;
  static constexpr int kMaxIters = 1000;
};

// Read a weights.cfg file.  Lines starting with '#' or that are empty are
// skipped.  Each data line must be "StrategyName weight".
vector<StrategyWeight> loadWeights(const string& path);

// Write weights to weights.cfg file
void saveWeights(const string& path, const vector<StrategyWeight>& weights);

} // namespace gradient_descent
} // namespace avengine
