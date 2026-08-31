#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace spyglass {

struct Details;
struct Record;

constexpr std::string_view kCsvHeader = "No.,Time,Source,Destination,Id,Length,Info,Body\n";

std::string report_row(const Record &record);
std::string report_csv(const Details &details);
std::string report_details(const Details &details, bool bytes);
void field_line(std::string &out, std::string_view key, const nlohmann::ordered_json &value);
std::string report_node(const nlohmann::ordered_json &node, int depth);
std::string report_failure(const nlohmann::ordered_json &error, int depth);

}  // namespace spyglass
