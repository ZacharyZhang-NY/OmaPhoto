#include "UI/CameraRawGeometryCalibrationControls.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawControls.h"
#include "UI/LayerIcons.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *text(const QString &words, int pixels, bool secondary)
{
    auto *label = new QLabel(words);
    QFont font = label->font();
    font.setPixelSize(pixels);
    label->setFont(font);
    label->setWordWrap(true);
    if (secondary)
        label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

// Swift's labelled Picker: its title, then the menu.
QComboBox *picker(const QString &title, const QStringList &choices, const QString &help, QWidget *parent, QVBoxLayout *column)
{
    auto *menu = new QComboBox(parent);
    menu->addItems(choices);
    menu->setToolTip(help);
    auto *label = new QLabel(title, parent);
    label->setBuddy(menu);
    auto *row = new QHBoxLayout;
    row->addWidget(label);
    row->addWidget(menu);
    row->addStretch(1);
    column->addLayout(row);
    return menu;
}

CameraRawSettings raw(const EditorSession &session)
{
    return session.filterEdit() ? session.filterEdit()->settings.cameraRaw : CameraRawSettings();
}

void write(EditorSession &session, const std::function<void(CameraRawSettings &)> &change)
{
    FilterSettings settings = session.filterEdit() ? session.filterEdit()->settings : FilterSettings();
    change(settings.cameraRaw);
    session.updateFilter(settings, session.filterEdit() ? session.filterEdit()->preview : true);
}

// Swift's Geometry and Calibration rows: the slider rounds.
CameraRawRow *rounded(const QString &name, const QString &title, const QString &help, double low, double high, std::function<double()> value,
                      std::function<void(double)> set, QWidget *parent)
{
    return new CameraRawRow({.name = name, .title = title, .help = help, .low = low, .high = high, .titleWidth = CameraRawControls::labelWidth, .scrub = 1},
                            std::move(value), [set](double number) { set(std::round(number)); }, set, [set] { set(0); }, parent);
}
}

CameraRawGeometryControls::CameraRawGeometryControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_guided(new QWidget(this)), m_draw(new QPushButton(QStringLiteral("Draw Guides"), m_guided)),
      m_drawHint(text(QStringLiteral("Drag on the layer to place a guide. Draw at least two lines."), 10, true)),
      m_clear(new QPushButton(QStringLiteral("Clear Guides"), m_guided)),
      m_constrain(new QCheckBox(QStringLiteral("Constrain Crop"), this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    column->addWidget(text(QStringLiteral("Upright"), 11, false));
    m_upright = cameraRawSegments({QStringLiteral("Off"), QStringLiteral("Guided")}, QStringLiteral("upright"),
                                  QStringLiteral("Off leaves the picture as it is. Guided straightens from lines you draw on the picture."), column, [this](int mode) {
                                      update([mode](CameraRawGeometrySettings &geometry) { geometry.upright = CameraRawUprightMode(mode); });
                                      if (CameraRawUprightMode(mode) != CameraRawUprightMode::guided && m_session.filterEdit()) {
                                          CameraRawPanel panel = m_session.filterEdit()->rawPanel;
                                          panel.drawingGeometryGuide = false;
                                          m_session.setCameraRawPanel(panel);
                                      }
                                  });
    auto *guided = new QVBoxLayout(m_guided);
    guided->setContentsMargins(0, 0, 0, 0);
    guided->setSpacing(8);
    m_draw->setObjectName(QStringLiteral("drawGuides"));
    m_draw->setCheckable(true);
    m_draw->setAutoDefault(false);
    m_draw->setToolTip(QStringLiteral("Draw two or more lines on the preview that should be level or vertical."));
    connect(m_draw, &QPushButton::clicked, this, [this] {
        if (!m_session.filterEdit())
            return;
        CameraRawPanel panel = m_session.filterEdit()->rawPanel;
        panel.drawingGeometryGuide = !panel.drawingGeometryGuide;
        m_session.setCameraRawPanel(panel);
    });
    guided->addWidget(m_draw, 0, Qt::AlignLeft);
    m_drawHint->setObjectName(QStringLiteral("drawGuidesHint"));
    guided->addWidget(m_drawHint);
    m_clear->setObjectName(QStringLiteral("clearGuides"));
    m_clear->setAutoDefault(false);
    m_clear->setToolTip(QStringLiteral("Remove every guide line."));
    connect(m_clear, &QPushButton::clicked, this, [this] { update([](CameraRawGeometrySettings &geometry) { geometry.guides.clear(); }); });
    guided->addWidget(m_clear, 0, Qt::AlignLeft);
    column->addWidget(m_guided);
    m_projection = picker(QStringLiteral("Projection"), {QStringLiteral("Perspective"), QStringLiteral("Rectilinear")},
                          QStringLiteral("Perspective allows stronger keystone. Rectilinear keeps the warp gentler."), this, column);
    m_projection->setObjectName(QStringLiteral("projection"));
    connect(m_projection, &QComboBox::activated, this,
            [this](int index) { update([index](CameraRawGeometrySettings &geometry) { geometry.projection = CameraRawProjection(index); }); });
    const auto slider = [this, column](const QString &name, const QString &title, double CameraRawGeometrySettings::*key, double low, double high,
                                       const QString &help) {
        auto *row = rounded(name, title, help, low, high, [this, key] { return raw(m_session).geometry.*key; },
                            [this, key](double value) { update([key, value](CameraRawGeometrySettings &geometry) { geometry.*key = value; }); }, this);
        column->addWidget(row);
        m_rows.push_back(row);
    };
    slider(QStringLiteral("vertical"), QStringLiteral("Vertical"), &CameraRawGeometrySettings::vertical, -100, 100,
           QStringLiteral("Straightens vertical lines toward the center."));
    slider(QStringLiteral("horizontal"), QStringLiteral("Horizontal"), &CameraRawGeometrySettings::horizontal, -100, 100,
           QStringLiteral("Straightens horizontal lines toward the center."));
    slider(QStringLiteral("rotate"), QStringLiteral("Rotate"), &CameraRawGeometrySettings::rotate, -45, 45, QStringLiteral("Rotates the picture around its center."));
    slider(QStringLiteral("aspect"), QStringLiteral("Aspect"), &CameraRawGeometrySettings::aspect, -100, 100, QStringLiteral("Stretches width relative to height."));
    slider(QStringLiteral("scale"), QStringLiteral("Scale"), &CameraRawGeometrySettings::scale, -100, 100,
           QStringLiteral("Zooms the transformed picture within the frame."));
    slider(QStringLiteral("offsetX"), QStringLiteral("Offset X"), &CameraRawGeometrySettings::offsetX, -100, 100, QStringLiteral("Moves the picture left or right."));
    slider(QStringLiteral("offsetY"), QStringLiteral("Offset Y"), &CameraRawGeometrySettings::offsetY, -100, 100, QStringLiteral("Moves the picture up or down."));
    m_constrain->setObjectName(QStringLiteral("constrainCrop"));
    m_constrain->setToolTip(QStringLiteral("Crops empty edges after the transform and fits the result back into the frame."));
    connect(m_constrain, &QAbstractButton::clicked, this, [this](bool on) { update([on](CameraRawGeometrySettings &geometry) { geometry.constrainCrop = on; }); });
    column->addWidget(m_constrain);
    connect(&m_session, &EditorSession::changed, this, &CameraRawGeometryControls::synchronize);
    synchronize();
}

void CameraRawGeometryControls::update(const std::function<void(CameraRawGeometrySettings &)> &change)
{
    write(m_session, [&change](CameraRawSettings &settings) { change(settings.geometry); });
}

void CameraRawGeometryControls::synchronize()
{
    const CameraRawGeometrySettings geometry = raw(m_session).geometry;
    const bool drawing = m_session.filterEdit() && m_session.filterEdit()->rawPanel.drawingGeometryGuide;
    m_upright->button(int(geometry.upright))->setChecked(true);
    m_guided->setVisible(geometry.upright == CameraRawUprightMode::guided);
    m_draw->setChecked(drawing);
    m_draw->setIcon(QIcon(LayerIcons::pixmap(LayerIcon::lineDiagonal, 14, palette().color(QPalette::ButtonText), devicePixelRatioF())));
    m_drawHint->setVisible(drawing);
    m_clear->setVisible(!geometry.guides.empty());
    const QSignalBlocker projection(m_projection), constrain(m_constrain);
    m_projection->setCurrentIndex(int(geometry.projection));
    m_constrain->setChecked(geometry.constrainCrop);
    for (CameraRawRow *row : m_rows)
        row->synchronize();
}

CameraRawCalibrationControls::CameraRawCalibrationControls(EditorSession &session, QWidget *parent)
    : QWidget(parent), m_session(session), m_process(new QComboBox(this)), m_summary(text(QString(), 10, true))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    for (int version = 0; version < 6; ++version)
        m_process->addItem(rawValue(CameraRawProcessVersion(version)));
    m_process->setObjectName(QStringLiteral("processVersion"));
    m_process->setToolTip(QStringLiteral("Chooses how strongly the calibration sliders below are applied. Version 6 is the current default."));
    auto *label = new QLabel(QStringLiteral("Process"), this);
    label->setBuddy(m_process);
    auto *processRow = new QHBoxLayout;
    processRow->addWidget(label);
    processRow->addWidget(m_process);
    processRow->addStretch(1);
    column->addLayout(processRow);
    connect(m_process, &QComboBox::activated, this, [this](int version) {
        write(m_session, [version](CameraRawSettings &settings) { settings.calibration.process = CameraRawProcessVersion(version); });
    });
    m_summary->setObjectName(QStringLiteral("processSummary"));
    column->addWidget(m_summary);
    const auto slider = [this, column](const QString &name, const QString &title, double CameraRawCalibrationSettings::*key, const QString &help) {
        auto *row = rounded(name, title, help, -100, 100, [this, key] { return raw(m_session).calibration.*key; },
                            [this, key](double value) { write(m_session, [key, value](CameraRawSettings &settings) { settings.calibration.*key = value; }); },
                            this);
        column->addWidget(row);
        m_rows.push_back(row);
    };
    column->addWidget(text(QStringLiteral("Shadows"), 11, false));
    slider(QStringLiteral("shadowTint"), QStringLiteral("Tint"), &CameraRawCalibrationSettings::shadowTint, QStringLiteral("Adds green or magenta to the darkest tones."));
    column->addWidget(text(QStringLiteral("Red Primary"), 11, false));
    slider(QStringLiteral("redHue"), QStringLiteral("Hue"), &CameraRawCalibrationSettings::redHue, QStringLiteral("Shifts how red is interpreted."));
    slider(QStringLiteral("redSaturation"), QStringLiteral("Saturation"), &CameraRawCalibrationSettings::redSaturation,
           QStringLiteral("Strengthens or weakens the red primary."));
    column->addWidget(text(QStringLiteral("Green Primary"), 11, false));
    slider(QStringLiteral("greenHue"), QStringLiteral("Hue"), &CameraRawCalibrationSettings::greenHue, QStringLiteral("Shifts how green is interpreted."));
    slider(QStringLiteral("greenSaturation"), QStringLiteral("Saturation"), &CameraRawCalibrationSettings::greenSaturation,
           QStringLiteral("Strengthens or weakens the green primary."));
    column->addWidget(text(QStringLiteral("Blue Primary"), 11, false));
    slider(QStringLiteral("blueHue"), QStringLiteral("Hue"), &CameraRawCalibrationSettings::blueHue, QStringLiteral("Shifts how blue is interpreted."));
    slider(QStringLiteral("blueSaturation"), QStringLiteral("Saturation"), &CameraRawCalibrationSettings::blueSaturation,
           QStringLiteral("Strengthens or weakens the blue primary."));
    connect(&m_session, &EditorSession::changed, this, &CameraRawCalibrationControls::synchronize);
    synchronize();
}

void CameraRawCalibrationControls::synchronize()
{
    const CameraRawCalibrationSettings calibration = raw(m_session).calibration;
    {
        const QSignalBlocker quiet(m_process);
        m_process->setCurrentIndex(int(calibration.process));
    }
    m_summary->setText(summary(calibration.process));
    m_summary->setToolTip(summary(calibration.process));
    for (CameraRawRow *row : m_rows)
        row->synchronize();
}
