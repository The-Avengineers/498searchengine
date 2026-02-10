#include "ranker.h" // Assuming your provided code is in Ranker.h
#include "string.h"
#include "vector.h"

using namespace avengine;

class DomainStrat : public Ranker::Strategy {
    public:
    double Score(avengine::string &url, const vector<vector<size_t>> &term_positions) {
        // Implementation: e.g., check if URL contains ".edu"
        for (size_t i = 0; i < GoodDomains.size(); i++) {
            if (url.find(GoodDomains[i]) != avengine::string::npos) {
                return 1.0;
            }
        }
        return 0.0;
    }
    private:
    const avengine::vector<const char*> GoodDomains = {".edu", ".gov"};
};

class ShortURLStrat : public Ranker::Strategy {
    // naïve implementation: take the reciprocal of the length of url
    public:
    double Score(avengine::string &url, const vector<vector<size_t>> &term_positions) {
        return url.size() == 0 ? 0.0 : 1 / double(url.size());
    }
};

class ShortTitleStrat : public Ranker::Strategy {

};