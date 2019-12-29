#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <nlohmann_json/json.hpp>

struct TDocument {
public:
    // Original fields
    std::string Title;
    std::string Url;
    std::string SiteName;
    std::string Description;
    std::string FileName;
    std::string Text;
    std::string Author;

    uint64_t PubTime = 0;
    uint64_t FetchTime = 0;

    std::vector<std::string> OutLinks;

    // Calculated fields
    std::string Language;
    std::string Category;
    bool IsNews;

public:
    TDocument() = default;
    TDocument(const char* fileName);

    nlohmann::json ToJson() const;
    void FromJson(
        const char* fileName
    );
    void FromHtml(
        const char* fileName,
        bool parseLinks=false,
        bool shrinkText=false,
        size_t maxWords=200
    );
};
