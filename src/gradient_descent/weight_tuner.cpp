#include <avengine/gradient_descent/weight_tuner.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace avengine
{
namespace gradient_descent
{

WeightTuner::WeightTuner(const vector<double>& initial_weights) : weights_(initial_weights) {}

double WeightTuner::linearScore(const vector<double>& features) const
{
  double score = 0.0;
  size_t n = features.size() < weights_.size() ? features.size() : weights_.size();
  for (size_t i = 0; i < n; ++i)
    score += weights_[i] * features[i];
  return score;
}

double WeightTuner::sampleLoss(const TrainingSample& sample) const
{
  double pos_score = linearScore(sample.positive_features);
  double loss = 0.0;
  for (size_t i = 0; i < sample.negative_features.size(); ++i)
  {
    double diff = linearScore(sample.negative_features[i]) - pos_score + kMargin;
    if (diff > 0.0)
      loss += diff;
  }
  return loss;
}

double WeightTuner::totalLoss(const vector<TrainingSample>& data) const
{
  double total = 0.0;
  for (size_t i = 0; i < data.size(); ++i)
    total += sampleLoss(data[i]);
  return total;
}

vector<double> WeightTuner::train(const vector<TrainingSample>& data)
{
  double step = 0.05;

  for (int iter = 0; iter < kMaxIters; ++iter)
  {
    double base_loss = totalLoss(data);

    for (size_t w = 0; w < weights_.size(); ++w)
    {
      double original = weights_[w];

      weights_[w] = original + step;
      double loss_up = totalLoss(data);

      weights_[w] = original - step;
      double loss_down = totalLoss(data);

      if (loss_up < loss_down && loss_up < base_loss)
        weights_[w] = original + step;
      else if (loss_down < loss_up && loss_down < base_loss)
        weights_[w] = original - step;
      else
        weights_[w] = original;
    }

    step *= 0.99;

    if (totalLoss(data) < 0.001)
      break;
  }

  // avoid negative weights
  for (size_t i = 0; i < weights_.size(); ++i)
    if (weights_[i] < 0.0)
      weights_[i] = 0.0;

  return weights_;
}

vector<StrategyWeight> loadWeights(const string& path)
{
  vector<StrategyWeight> result;

  FILE* f = fopen(path.cstr(), "r");
  if (!f)
    return result;

  char buf[256];
  while (fgets(buf, sizeof(buf), f))
  {
    // Strip trailing \r\n.
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
      buf[--len] = '\0';

    // Skip leading whitespace.
    const char* p = buf;
    while (*p == ' ' || *p == '\t')
      ++p;

    if (*p == '\0' || *p == '#')
      continue;

    // Parse "StrategyName weight"
    char name[128] = {};
    double weight = 1.0;
    if (sscanf(p, "%127s %lf", name, &weight) < 2)
      continue;

    StrategyWeight sw;
    sw.name = avengine::string(name);
    sw.weight = weight;
    result.pushBack(sw);
  }

  fclose(f);
  return result;
}

void saveWeights(const string& path, const vector<StrategyWeight>& weights)
{
  FILE* f = fopen(path.cstr(), "w");
  if (!f)
    return;

  fprintf(f, "# weights.cfg\n");
  fprintf(f, "# format: StrategyName weight\n");

  for (size_t i = 0; i < weights.size(); ++i)
  {
    // Left-pad name to 20 chars for readability.
    const char* name = weights[i].name.cstr();
    size_t name_len = strlen(name);
    fprintf(f, "%s", name);
    for (size_t pad = name_len; pad < 20; ++pad)
      fputc(' ', f);
    fprintf(f, "%.15g\n", weights[i].weight);
  }

  fclose(f);
}

} // namespace gradient_descent
} // namespace avengine
