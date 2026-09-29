#pragma once
#include "Document/TypeTool.h"
#include <QByteArray>
#include <QImage>
#include <map>
#include <optional>
#include <vector>

// Photoshop 6 type (`TySh`) read into the text model.
namespace PSDText {
struct Source {
    LayerTextStyle style;
    std::vector<QString> notes;
    // The document point the image's anchor lands on.
    QPointF documentAnchor;
    double rotation = 0;
    bool flipY = false;
    // The paragraph frame's corner, else point text's baseline.
    bool anchorIsFrame = false;
};

struct Rendered {
    QImage image;
    LayerTransform transform;
};

inline const QString rasterizedNote = QStringLiteral("Editable Photoshop text becomes pixels and can’t be retyped.");
inline const QString firstStyleNote = QStringLiteral("Only the first text style was kept.");
inline const QString warpNote = QStringLiteral("The Photoshop text warp was omitted.");
inline const QString fauxNote = QStringLiteral("Faux bold or faux italic was omitted.");
inline const QString justifyNote = QStringLiteral("Full justification was imported as left alignment.");

std::optional<QString> missingFontNote(const QString &name);
std::optional<Source> parse(const std::map<QString, QByteArray> &extra);
Rendered render(const Source &source);
}
