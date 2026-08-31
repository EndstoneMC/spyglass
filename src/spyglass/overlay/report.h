#pragma once

#include <string>
#include <string_view>

namespace spyglass {

struct Details;
struct Node;
struct Record;

constexpr std::string_view kCsvHeader = "No.,Time,Source,Destination,Id,Length,Info,Body\n";

std::string report_row(const Record &record);
std::string report_csv(const Details &details);
std::string report_details(const Details &details, bool bytes);
std::string report_node(const Node &node, int depth);

}  // namespace spyglass
