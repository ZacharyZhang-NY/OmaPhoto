#pragma once
#include "Document/Filters.h"
#include <stdexcept>

// Swift's ContentFill.Failure.
class ContentFillError : public std::runtime_error {
public:
    enum class Kind { noSource };
    explicit ContentFillError(Kind kind);
    const Kind kind;
};

// Content-Aware Fill: the selection made from the pixels around it.
namespace ContentFill {
QImage run(const FilterJob &job);
}
