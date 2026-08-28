#pragma once

#include <string>

#include "spyglass/overlay/bytes.h"

namespace spyglass {

class Capture;
struct Filter;

enum class Export : int {
    DisplayedText = 0,
    DisplayedCsv = 1,
    SelectedDetails = 2,
    SelectedBytes = 3,
    Summary = 4,
};

std::string export_capture(const Capture &capture, const Filter &filter, Export what, BytesFormat bytes);

}  // namespace spyglass
