#pragma once

#include <string>
#include <string_view>

namespace spyglass {

struct Node;
struct Record;

constexpr std::string_view kCsvHeader = "No.,Time,Source,Destination,Id,Length,Info,Body\n";

std::string report_row(const Record &record);
std::string report_csv(const Record &record);
std::string report_details(const Record &record);
std::string report_node(const Node &node, int depth);

}  // namespace spyglass
