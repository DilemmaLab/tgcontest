#include <Eigen/Core>
#include <cassert>
#include "../util.h"
#include "rank_docs.h"
#include "clustering.h"


std::unordered_map<std::string, double> LoadRatings(const std::string& ratingPath) {
    std::unordered_map<std::string, double> output;
    std::string line;

    std::ifstream rating(ratingPath);
    if (rating.is_open()) {
        while (std::getline(rating, line)) {
            std::vector<std::string> lineSplitted;
            boost::split(lineSplitted, line, boost::is_any_of("\t"));

            output[lineSplitted[1]] = std::stod(lineSplitted[0]);
        }
    } else {
        LOG_DEBUG("Rating file is not available")
    }
    return output;
}

double ComputeDocAgencyWeight(
    const TDocument& doc,
    const std::unordered_map<std::string, double>& agencyRating
) {
    const auto host = GetHost(doc.Url);
    const auto iter = agencyRating.find(host);

    if (iter != agencyRating.end()) {
        return iter->second;
    }

    return 0.000015;
}

double ComputeDocWeight(
    const TDocument& doc,
    const std::unordered_map<std::string, double>& agencyRating,
    const double docRelevance,
    const uint64_t freshestTimestamp,
    const bool useTimeMultiplier
) {
    // ~ 1 for freshest doc, 0.5 for 12 hour late, ~0 for 24 hour late doc
    const double timeMultiplier = useTimeMultiplier ? Sigmoid((static_cast<double>(doc.FetchTime) - static_cast<double>(freshestTimestamp)) / 3600.0 + 12) : 1.0;

    return (ComputeDocAgencyWeight(doc, agencyRating) + docRelevance) * timeMultiplier;
}

std::vector<TNewsCluster> RankClustersDocs(
    const TClustering::TClusters& clusters,
    const std::unordered_map<std::string, double>& agencyRating,
    const TFastTextEmbedder& ruModel,
    const TFastTextEmbedder& enModel
) {
    std::vector<TNewsCluster> output;

    for (auto& cluster : clusters) {
        std::vector<WeightedDoc> weightedDocs;
        uint64_t freshestTimestamp = cluster.GetFreshestTimestamp();

        Eigen::MatrixXf docsCosine; // NxN matrix with cosine titles

        assert(cluster.GetSize() != 0);
        size_t embSize = cluster.GetDocuments()[0].get().IsRussian() ? ruModel.GetEmbeddingSize() : enModel.GetEmbeddingSize();

        Eigen::MatrixXf points(cluster.GetSize(), embSize);
        for (size_t i = 0; i < cluster.GetSize(); i++) {
            const TDocument& doc = cluster.GetDocuments()[i];
            const TFastTextEmbedder& model = doc.IsRussian() ? ruModel : enModel;
            fasttext::Vector embedding = model.GetSentenceEmbedding(doc);
            Eigen::Map<Eigen::VectorXf, Eigen::Unaligned> eigenVector(embedding.data(), embedding.size());
            points.row(i) = eigenVector / eigenVector.norm();
        }
        docsCosine = points * points.transpose();

        for (size_t i = 0; i < cluster.GetSize(); ++i) {
            const TDocument& doc = cluster.GetDocuments()[i];
            const double docRelevance = docsCosine.row(i).mean();
            double weight = ComputeDocWeight(
                doc,
                agencyRating,
                docRelevance,
                freshestTimestamp,
                /* useTimeMultiplier = */ true
            );

            weightedDocs.emplace_back(doc, weight);
        }
        std::stable_sort(weightedDocs.begin(), weightedDocs.end(), [](WeightedDoc a, WeightedDoc b) {
            return a.Weight > b.Weight;
        });
        TNewsCluster clusterSorted;
        for (const WeightedDoc& elem : weightedDocs) {
            clusterSorted.AddDocument(elem.Doc);
        }
        output.emplace_back(std::move(clusterSorted));
    }
    return output;
}
