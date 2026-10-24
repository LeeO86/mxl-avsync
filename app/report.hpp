#pragma once

#include "avsync/avsync.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace avsync {

nlohmann::json measurement_to_json(const Measurement& measurement);
nlohmann::json statistics_to_json(const SeriesStats& stats);
nlohmann::json result_document(const std::string& test_id, const std::string& mode, const std::string& graded_on,
    const ReferenceInfo& reference, const nlohmann::json& stream, const std::vector<Measurement>& events);
std::string human_report(const nlohmann::json& result);
nlohmann::json merge_result_documents(const nlohmann::json& source, const nlohmann::json& destination);

} // namespace avsync
