#include <set>
#include <cmath>
#include <queue>
#include "rank.h"
#include "../util.h"
#include "../clustering/rank_docs.h"

uint64_t GetIterTimestamp(const std::vector<TNewsCluster>& clusters) {
    // in production here should be ts.now().
    // but here we have 0.995 percentile of doc timestamps because of small percent of wrong dates
    std::priority_queue<uint64_t, std::vector<uint64_t>, std::greater<uint64_t>> timestamps;
    const float PERCENTILE = 0.99;
    uint64_t numDocs = 0;
    for (const auto& cluster: clusters) {
        numDocs += cluster.size();
    }
    size_t prioritySize = numDocs - std::floor(PERCENTILE * numDocs);

    for (const auto& cluster : clusters) {
        for (const auto& doc: cluster) {
            timestamps.push(doc.get().FetchTime);
            if (timestamps.size() > prioritySize) {
                timestamps.pop();
            }
        }
    }

    return timestamps.size() > 0 ? timestamps.top() : 0;
}

std::string ComputeClusterCategory(const TNewsCluster& cluster) {
    std::unordered_map<std::string, size_t> categoryCount;
    for (const auto& doc : cluster) {
        std::string docCategory = doc.get().Category;
        if (categoryCount.find(docCategory) == categoryCount.end()) {
            categoryCount[docCategory] = 0;
        }
        categoryCount[docCategory] += 1;
    }
    std::vector<std::pair<std::string, size_t>> categoryCountVector(categoryCount.begin(), categoryCount.end());
    std::sort(categoryCountVector.begin(), categoryCountVector.end(), [](std::pair<std::string, size_t> a, std::pair<std::string, size_t> b) {
        return a.second > b.second;
    });

    return categoryCountVector[0].first;
}

double ComputeClusterWeight(
    const TNewsCluster& cluster,
    const std::unordered_map<std::string, double>& agencyRating,
    const uint64_t iterTimestamp
) {
    double output = 0.0;
    const float clusterTimestampPercentile = 0.9;
    std::set<std::string> seenHosts;

    std::vector<uint64_t> clusterTimestamps;

    for (const auto& doc : cluster) {
        if (seenHosts.insert(GetHost(doc.get().Url)).second) {
            output += ComputeDocAgencyWeight(doc, agencyRating);
        }
        clusterTimestamps.push_back(doc.get().FetchTime);
    }

    std::sort(clusterTimestamps.begin(), clusterTimestamps.end());

    double clusterTimestamp = clusterTimestamps[static_cast<size_t>(std::floor(clusterTimestampPercentile * (clusterTimestamps.size() - 1)))];
    double clusterTimestampRemapped = (clusterTimestamp - iterTimestamp) / 3600.0 + 12;
    double timeMultiplier = Sigmoid(clusterTimestampRemapped); // ~1 for freshest ts, 0.5 for 12 hour old ts, ~0 for 24 hour old ts

    const size_t clusterSize = cluster.size();
    double smallClusterCoef = 1.0;
    if (clusterSize == 1) {
        smallClusterCoef = 0.2;
    } else if (clusterSize == 2) {
        smallClusterCoef = 0.4;
    } else if (clusterSize == 3) {
        smallClusterCoef = 0.6;
    } else if (clusterSize == 4) {
        smallClusterCoef = 0.8;
    } // pessimize only clusters with size < 5

    return output * timeMultiplier * smallClusterCoef;
}


std::unordered_map<std::string, std::vector<TWeightedNewsCluster>> Rank(
    const std::vector<TNewsCluster>& clusters,
    const std::unordered_map<std::string, double>& agencyRating
) {
    std::vector<std::string> categoryList = {"any", "society", "economy", "technology", "sports", "entertainment", "science", "other"};
    std::unordered_map<std::string, std::vector<TWeightedNewsCluster>> output;
    std::vector<TWeightedNewsCluster> weightedClusters;

    uint64_t iterTimestamp = GetIterTimestamp(clusters);

    for (const auto& cluster : clusters) {
        const std::string clusterCategory = ComputeClusterCategory(cluster);
        const double weight = ComputeClusterWeight(cluster, agencyRating, iterTimestamp);
        const std::string title = cluster[0].get().Title;
        weightedClusters.emplace_back(cluster, clusterCategory, title, weight);
    }

    std::sort(weightedClusters.begin(), weightedClusters.end(), [](const TWeightedNewsCluster a, const TWeightedNewsCluster b) {
        return a.Weight > b.Weight;
    });

    for (const auto& category : categoryList) {
        std::vector<TWeightedNewsCluster> categoryWeightedClusters;
        std::copy_if(
            weightedClusters.cbegin(),
            weightedClusters.cend(),
            std::back_inserter(categoryWeightedClusters),
            [&category](const TWeightedNewsCluster a) {
                return ((a.Category == category) || (category == "any")) ? true : false;
            }
        );
        output[category] = std::move(categoryWeightedClusters);
    }

    return output;
}
