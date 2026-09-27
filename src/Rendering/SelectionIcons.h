#pragma once
#include <QPainter>

// Which selection tool an icon names, by its kind.
enum class SelectionIcon { freehandLasso, polygonalLasso, rectangleMarquee, ellipseMarquee, objectSelection };

// Icons the rail and the cursors share, 18-unit grid.
namespace SelectionIcons {
// Draws with the painter's pen; the Marquee's take a dash.
void paint(QPainter &painter, SelectionIcon icon);
}
