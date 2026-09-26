#pragma once
#include "IO/ImageExporter.h"
#include "SelectionCanvasFixtures.h"

// Shared by the brush's canvas tests: pixels, a red brush.
inline std::vector<int> pixel(const EditorSession &session, int x, int y)
{
    const QImage image = ImageExporter::render(session.projectSnapshot().value()).image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const uchar *at = image.constScanLine(y) + x * 4;
    return {at[0], at[1], at[2], at[3]};
}

// A blank layer, and a hard 10 px red brush.
inline void red(Canvas &shown)
{
    shown.session.addBlankLayer();
    shown.session.selectTool(NavigationTool::brush);
    BrushSettings settings = shown.session.brushSettings();
    settings.diameter = 10;
    settings.red = 1;
    shown.session.setBrushSettings(settings);
}
