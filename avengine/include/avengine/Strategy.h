#include "ranker.h" // Assuming your provided code is in Ranker.h
#include "string.h"
#include "vector.h"

using namespace avengine;

class DomainStrat : public Ranker::Strategy {
    public:
    double Score(avengine::string &url, const vector<vector<size_t>> &term_positions) {
        // Implementation: e.g., check if URL contains ".edu"
        if (url.find(".edu") != avengine::string::npos) {
            return 1.0;
        }
        if (url.find(".gov") != avengine::string::npos) {
            return 1.0;
        }
        return 0.0;
    }
};