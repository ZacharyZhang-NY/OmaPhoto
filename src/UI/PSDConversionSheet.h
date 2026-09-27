#pragma once
#include "IO/PSD/PSDTypes.h"
#include <QWidget>
#include <functional>

struct PSDConversionRequest {
    QString title;
    QString confirmTitle;
    std::vector<PSDConversion> conversions;
    // Still read: the sheet answers the click, reporting nothing.
    bool isReading = false;
    friend bool operator==(const PSDConversionRequest &, const PSDConversionRequest &) = default;
};

// What an import converts, confirmed before anything applies.
class PSDConversionSheet : public QWidget {
    Q_OBJECT
public:
    PSDConversionSheet(const PSDConversionRequest &request, std::function<void(bool)> finish, QWidget *parent = nullptr);
};
