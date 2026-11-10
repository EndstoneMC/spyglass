#include "spyglass/overlay/filter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {
namespace {

constexpr std::size_t kFilterOps = 64;

constexpr std::array<std::pair<std::string_view, char>, 3> kLogical{{{"and", '&'}, {"or", '|'}, {"not", '!'}}};

constexpr std::array<std::pair<std::string_view, FilterCompare>, 7> kRelations{{
    {"eq", FilterCompare::Equal},
    {"ne", FilterCompare::NotEqual},
    {"lt", FilterCompare::Less},
    {"le", FilterCompare::LessEqual},
    {"gt", FilterCompare::Greater},
    {"ge", FilterCompare::GreaterEqual},
    {"contains", FilterCompare::Contains},
}};

}  // namespace

FilterProgram parse_filter(const std::string_view text)
{
    FilterProgram program;
    std::vector<char> pending;
    auto expect_term = true;
    const FilterField *field = nullptr;
    auto relation = FilterCompare::Equal;
    auto compared = false;

    const auto same = [](const char a, const char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    const auto emit = [&](const char op) {
        program.ops.push_back({.term = op == '!' ? FilterTerm::Not : op == '&' ? FilterTerm::And : FilterTerm::Or});
    };
    const auto word_char = [](const char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
    };
    const auto &names = packet_names();

    std::size_t at = 0;
    while (program.error.empty()) {
        while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) {
            ++at;
        }
        if (at == text.size()) {
            break;
        }

        const auto start = at;
        const auto rest = text.substr(at);
        char op = 0;
        std::optional<FilterCompare> compare;
        std::optional<std::string_view> quoted;
        std::string_view word;
        if (rest.starts_with("&&") || rest.starts_with("||")) {
            op = rest[0];
            at += 2;
        }
        else if (rest.starts_with("==") || rest.starts_with("!=") || rest.starts_with("<=") || rest.starts_with(">=")) {
            compare = rest[0] == '=' ? FilterCompare::Equal
                      : rest[0] == '!' ? FilterCompare::NotEqual
                      : rest[0] == '<' ? FilterCompare::LessEqual
                                       : FilterCompare::GreaterEqual;
            at += 2;
        }
        else if (rest[0] == '<' || rest[0] == '>') {
            compare = rest[0] == '<' ? FilterCompare::Less : FilterCompare::Greater;
            ++at;
        }
        else if (rest[0] == '!' || rest[0] == '(' || rest[0] == ')') {
            op = rest[0];
            ++at;
        }
        else if (rest[0] == '"') {
            const auto close = text.find('"', at + 1);
            if (close == std::string_view::npos) {
                program.error = "Missing closing quote.";
                break;
            }
            quoted = text.substr(at + 1, close - at - 1);
            at = close + 1;
        }
        else if (word_char(rest[0])) {
            while (at < text.size() && word_char(text[at])) {
                ++at;
            }
            word = text.substr(start, at - start);
        }
        else {
            program.error = std::format("\"{}\" was unexpected in this context.", rest[0]);
            break;
        }
        const auto token = text.substr(start, at - start);

        if (!word.empty() && (field == nullptr || !compared)) {
            for (const auto &[spelling, meaning] : kRelations) {
                if (std::ranges::equal(word, spelling, same)) {
                    compare = meaning;
                    word = {};
                }
            }
        }
        if (!word.empty() && field == nullptr) {
            for (const auto &[spelling, logical] : kLogical) {
                if (std::ranges::equal(word, spelling, same)) {
                    op = logical;
                    word = {};
                }
            }
        }

        std::optional<FilterOp> term;
        std::string_view needle;
        auto by_name = false;
        auto substring = false;
        auto negate = false;
        auto bare = false;

        if (field != nullptr) {
            if (compare && !compared) {
                if (field->kind == FilterKind::Text && *compare != FilterCompare::Equal &&
                    *compare != FilterCompare::NotEqual && *compare != FilterCompare::Contains) {
                    program.error = std::format("{} can only be compared with ==, != or contains.", field->name);
                    break;
                }
                if (field->kind == FilterKind::Number && *compare == FilterCompare::Contains) {
                    program.error = std::format("{} cannot be compared with contains.", field->name);
                    break;
                }
                relation = *compare;
                compared = true;
                continue;
            }
            if (!compared) {
                program.error = field->kind == FilterKind::Text
                                    ? std::format("{} needs a comparison, such as {} contains \"Move\".", field->name,
                                                  field->name)
                                    : std::format("{} needs a comparison, such as {} > 100.", field->name, field->name);
                break;
            }
            if (field->kind == FilterKind::Number) {
                const auto hex = word.size() > 2 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X');
                const auto digits = hex ? word.substr(2) : word;
                std::int64_t value = 0;
                const auto [end, ec] =
                    std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
                if (word.empty() || ec != std::errc{} || end != digits.data() + digits.size()) {
                    program.error = std::format("\"{}\" is not a valid number.", token);
                    break;
                }
                term = FilterOp{.term = field->term, .compare = relation, .value = value};
            }
            else if (quoted || !word.empty()) {
                needle = quoted ? *quoted : word;
                by_name = true;
                substring = relation == FilterCompare::Contains;
                negate = relation == FilterCompare::NotEqual;
            }
            else {
                program.error = std::format("\"{}\" was unexpected in this context.", token);
                break;
            }
            field = nullptr;
            compared = false;
        }
        else if (compare || quoted) {
            program.error = std::format("\"{}\" was unexpected in this context.", token);
            break;
        }
        else if (!word.empty()) {
            if (!expect_term) {
                program.error = std::format("\"{}\" was unexpected in this context.", token);
                break;
            }
            const auto known = std::ranges::find_if(
                kFilterFields, [&](const FilterField &candidate) { return std::ranges::equal(word, candidate.name, same); });
            if (known != kFilterFields.end() && known->kind == FilterKind::Flag) {
                term = FilterOp{.term = known->term};
            }
            else if (known != kFilterFields.end()) {
                field = &*known;
                continue;
            }
            else {
                needle = word;
                by_name = true;
                bare = true;
            }
        }

        if (by_name) {
            term = FilterOp{.term = FilterTerm::Packets};
            term->packets.resize(names.size(), 0);
            constexpr std::string_view kSuffix{"Packet"};
            for (std::size_t id = 0; id < names.size(); ++id) {
                const std::string_view name{names[id]};
                const auto hit = substring ? !std::ranges::search(name, needle, same).empty()
                                           : std::ranges::equal(name, needle, same) ||
                                                 (name.size() == needle.size() + kSuffix.size() &&
                                                  std::ranges::equal(name.substr(0, needle.size()), needle, same) &&
                                                  std::ranges::equal(name.substr(needle.size()), kSuffix, same));
                term->packets[id] = hit ? 1 : 0;
            }
            if (bare && std::ranges::find(term->packets, std::uint8_t{1}) == term->packets.end()) {
                program.error = std::format("\"{}\" is neither a packet nor a field.", needle);
                break;
            }
        }

        if (term) {
            if (!expect_term) {
                program.error = std::format("\"{}\" was unexpected in this context.", token);
                break;
            }
            program.ops.push_back(std::move(*term));
            if (negate) {
                program.ops.push_back({.term = FilterTerm::Not});
            }
            expect_term = false;
            continue;
        }

        switch (op) {
        case '(':
        case '!':
            if (!expect_term) {
                program.error = std::format("\"{}\" was unexpected in this context.", token);
                break;
            }
            pending.push_back(op);
            break;
        case '&':
        case '|':
            if (expect_term) {
                program.error = std::format("\"{}\" was unexpected in this context.", token);
                break;
            }
            while (!pending.empty() && pending.back() != '(' && (op == '|' || pending.back() != '|')) {
                emit(pending.back());
                pending.pop_back();
            }
            pending.push_back(op);
            expect_term = true;
            break;
        case ')':
            while (!expect_term && !pending.empty() && pending.back() != '(') {
                emit(pending.back());
                pending.pop_back();
            }
            if (expect_term || pending.empty()) {
                program.error = "\")\" was unexpected in this context.";
                break;
            }
            pending.pop_back();
            break;
        default:
            break;
        }
    }

    if (program.error.empty() && field != nullptr && !compared) {
        program.error = field->kind == FilterKind::Text
                            ? std::format("{} needs a comparison, such as {} contains \"Move\".", field->name,
                                          field->name)
                            : std::format("{} needs a comparison, such as {} > 100.", field->name, field->name);
    }
    if (program.error.empty() && field != nullptr) {
        program.error = "Unexpected end of filter expression.";
    }
    if (program.error.empty() && expect_term && (!program.ops.empty() || !pending.empty())) {
        program.error = "Unexpected end of filter expression.";
    }
    while (program.error.empty() && !pending.empty()) {
        if (pending.back() == '(') {
            program.error = "Missing closing parenthesis.";
            break;
        }
        emit(pending.back());
        pending.pop_back();
    }
    if (program.error.empty() && program.ops.size() > kFilterOps) {
        program.error = "The filter is too long.";
    }
    if (!program.error.empty()) {
        program.ops.clear();
    }
    return program;
}

bool Filter::active() const
{
    return !program.empty();
}

bool Filter::matches(const Record &record) const
{
    if (program.empty()) {
        return true;
    }

    std::array<bool, kFilterOps> stack{};
    std::size_t depth = 0;
    for (const auto &op : program) {
        switch (op.term) {
        case FilterTerm::Packets:
            stack[depth++] = record.id >= 0 && static_cast<std::size_t>(record.id) < op.packets.size() &&
                             op.packets[static_cast<std::size_t>(record.id)] != 0;
            break;
        case FilterTerm::Id:
        case FilterTerm::Length:
        case FilterTerm::Number:
        case FilterTerm::Unread: {
            const auto value = op.term == FilterTerm::Id       ? static_cast<std::int64_t>(record.id)
                               : op.term == FilterTerm::Length ? static_cast<std::int64_t>(record.length)
                               : op.term == FilterTerm::Number ? static_cast<std::int64_t>(record.number)
                                                               : static_cast<std::int64_t>(record.unread);
            switch (op.compare) {
            case FilterCompare::Equal:
                stack[depth++] = value == op.value;
                break;
            case FilterCompare::NotEqual:
                stack[depth++] = value != op.value;
                break;
            case FilterCompare::Less:
                stack[depth++] = value < op.value;
                break;
            case FilterCompare::LessEqual:
                stack[depth++] = value <= op.value;
                break;
            case FilterCompare::Greater:
                stack[depth++] = value > op.value;
                break;
            case FilterCompare::GreaterEqual:
                stack[depth++] = value >= op.value;
                break;
            case FilterCompare::Contains:
                stack[depth++] = false;
                break;
            }
            break;
        }
        case FilterTerm::Failed:
            stack[depth++] = !record.decoded;
            break;
        case FilterTerm::Sent:
            stack[depth++] = record.direction == Direction::Outbound;
            break;
        case FilterTerm::Received:
            stack[depth++] = record.direction == Direction::Inbound;
            break;
        case FilterTerm::Not:
            stack[depth - 1] = !stack[depth - 1];
            break;
        case FilterTerm::And:
            --depth;
            stack[depth - 1] = stack[depth - 1] && stack[depth];
            break;
        case FilterTerm::Or:
            --depth;
            stack[depth - 1] = stack[depth - 1] || stack[depth];
            break;
        }
    }
    return stack[0];
}

}  // namespace spyglass
