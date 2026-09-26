#include "UI/BrushControls.h"
#include "Document/EditorSession.h"
#include "UI/ColorPaletteControls.h"
#include "UI/LassoControls.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <cmath>

namespace {
// The session's settings with one field rewritten.
void changeBrush(EditorSession &session, const std::function<void(BrushSettings &)> &change)
{
    BrushSettings settings = session.brushSettings();
    change(settings);
    session.setBrushSettings(settings);
}

const QString modeTip = QStringLiteral("Paint with the foreground color (B), or erase pixels away (E)");
const QString blurTip = QStringLiteral("Liquify pushes pixels · Blur softens · Smudge drags color along");
const QString sampleTip = QStringLiteral("Copy from the active layer only, or from every visible layer as shown");

QLabel *label(const QString &text, QWidget *parent)
{
    auto *made = new QLabel(text, parent);
    made->setFont(ToolHeaderStyle::controlFont());
    return made;
}
}

BrushControls::BrushControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QString(), parent), m_session(session), m_modes(new QButtonGroup(this)),
      m_paint(choice(m_modes, QStringLiteral("brushPaint"), rawValue(BrushToolMode::paint), modeTip, [this] { m_session.setBrushMode(BrushToolMode::paint); })),
      m_erase(choice(m_modes, QStringLiteral("brushErase"), rawValue(BrushToolMode::erase), modeTip, [this] { m_session.setBrushMode(BrushToolMode::erase); })),
      m_blurModes(new QButtonGroup(this)), m_healingTypes(new QButtonGroup(this)), m_aligned(new QCheckBox(QStringLiteral("Aligned"), this)), m_samples(new QButtonGroup(this)),
      m_thisLayer(choice(m_samples, QStringLiteral("cloneThisLayer"), QStringLiteral("This Layer"), sampleTip,
                         [this] { m_session.setCloneSettings(CloneSettings{m_session.cloneSettings().aligned, false}); })),
      m_allLayers(choice(m_samples, QStringLiteral("cloneAllLayers"), QStringLiteral("All Layers"), sampleTip,
                         [this] { m_session.setCloneSettings(CloneSettings{m_session.cloneSettings().aligned, true}); })),
      m_size(new SelectionAmountField(session, 1, 2000, [this] { return m_session.brushSettings().diameter; },
                                      [this](double size) { changeBrush(m_session, [size](BrushSettings &brush) { brush.diameter = size; }); }, this)),
      m_hardnessSlider(slider(QStringLiteral("brushHardnessSlider"), 0, 1000,
                              [this](int value) { changeBrush(m_session, [value](BrushSettings &brush) { brush.hardness = value / 1000.0; }); })),
      m_hardness(new SelectionAmountField(session, 0, 100, [this] { return m_session.brushSettings().hardness * 100; },
                                          [this](double percent) { changeBrush(m_session, [percent](BrushSettings &brush) { brush.hardness = percent / 100; }); }, this)),
      m_opacityLabel(label(QStringLiteral("Opacity"), this)),
      m_opacitySlider(slider(QStringLiteral("brushOpacitySlider"), 10, 1000,
                             [this](int value) { changeBrush(m_session, [value](BrushSettings &brush) { brush.opacity = value / 1000.0; }); })),
      m_opacity(new SelectionAmountField(session, 1, 100, [this] { return m_session.brushSettings().opacity * 100; },
                                         [this](double percent) { changeBrush(m_session, [percent](BrushSettings &brush) { brush.opacity = percent / 100; }); }, this)),
      m_paintLabel(label(QStringLiteral("Paint"), this)), m_maskPaint(new QComboBox(this)), m_colourLabel(label(QStringLiteral("Color"), this)),
      m_colour(new SwatchButton([&session] { return session.foregroundColor(); }, 4, 1, 1, this)),
      m_cloneNote(label(QStringLiteral("Alt-click to set the source"), this)), m_mask(label(QStringLiteral("Mask"), this))
{
    m_blurModes->setObjectName(QStringLiteral("blurMode"));
    for (const BlurToolMode mode : allBlurToolModes)
        choice(m_blurModes, QString(), rawValue(mode), blurTip, [this, mode] { m_session.setBlurMode(mode); });
    m_healingTypes->setObjectName(QStringLiteral("spotHealingType"));
    for (const SpotHealingMode type : allSpotHealingModes)
        choice(m_healingTypes, QString(), rawValue(type), QString(), [this, type] { m_session.setSpotHealingMode(type); });
    m_aligned->setObjectName(QStringLiteral("cloneAligned"));
    m_aligned->setToolTip(QStringLiteral("Keep the source moving with the brush between strokes; off starts every stroke at the source point"));
    connect(m_aligned, &QCheckBox::clicked, this,
            [this](bool on) { m_session.setCloneSettings(CloneSettings{on, m_session.cloneSettings().sampleAllLayers}); });
    m_size->setObjectName(QStringLiteral("brushSize"));
    m_size->setFixedWidth(48);
    m_hardness->setObjectName(QStringLiteral("brushHardness"));
    m_hardness->setFixedWidth(42);
    m_opacity->setObjectName(QStringLiteral("brushOpacity"));
    m_opacity->setFixedWidth(42);
    m_opacity->setToolTip(QStringLiteral("Press 1–9 for 10–90%, 0 for 100%"));
    m_maskPaint->setObjectName(QStringLiteral("maskPaint"));
    m_maskPaint->setFixedWidth(180);
    m_maskPaint->addItem(QStringLiteral("Black · Hide"), false);
    m_maskPaint->addItem(QStringLiteral("White · Reveal"), true);
    connect(m_maskPaint, &QComboBox::activated, this, [this](int index) { m_session.setMaskPaintWhite(index == 1); });
    m_cloneNote->setObjectName(QStringLiteral("cloneSourceNote"));
    m_cloneNote->setForegroundRole(QPalette::PlaceholderText);
    m_mask->setObjectName(QStringLiteral("brushMaskNote"));
    m_mask->setForegroundRole(QPalette::PlaceholderText);
    m_colour->setObjectName(QStringLiteral("brushColor"));
    m_colour->setFixedSize(34, 18);
    m_colour->setToolTip(QStringLiteral("Foreground color"));
    m_colour->setAccessibleName(QStringLiteral("Foreground color"));
    connect(m_colour, &QAbstractButton::clicked, this, [this] { m_session.openColorPicker(false); });
    std::vector<QWidget *> widgets{m_paint, m_erase};
    for (QAbstractButton *mode : m_blurModes->buttons())
        widgets.push_back(mode);
    for (QAbstractButton *type : m_healingTypes->buttons())
        widgets.push_back(type);
    widgets.insert(widgets.end(), {m_aligned, m_thisLayer, m_allLayers});
    widgets.insert(widgets.end(), {label(QStringLiteral("Size"), this), m_size, label(QStringLiteral("px"), this), label(QStringLiteral("Hardness"), this),
                                   m_hardnessSlider, m_hardness, label(QStringLiteral("%"), this), m_opacityLabel, m_opacitySlider, m_opacity,
                                   label(QStringLiteral("%"), this), m_paintLabel, m_maskPaint, m_colourLabel, m_colour});
    for (QWidget *widget : widgets)
        row->insertWidget(row->count() - 1, widget);
    row->addWidget(m_cloneNote);
    row->addWidget(m_mask);
    connect(&m_session, &EditorSession::changed, this, &BrushControls::synchronize);
    synchronize();
}

QToolButton *BrushControls::choice(QButtonGroup *group, const QString &name, const QString &text, const QString &tip, const std::function<void()> &pick)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(text);
    button->setCheckable(true);
    button->setToolTip(tip);
    group->addButton(button);
    connect(button, &QToolButton::clicked, this, pick);
    return button;
}

QSlider *BrushControls::slider(const QString &name, int low, int high, const std::function<void(int)> &change)
{
    auto *made = new QSlider(Qt::Horizontal, this);
    made->setObjectName(name);
    made->setRange(low, high);
    made->setFixedWidth(100);
    connect(made, &QSlider::valueChanged, this, change);
    return made;
}

void BrushControls::synchronize()
{
    const BrushSettings &brush = m_session.brushSettings();
    const NavigationTool tool = m_session.tool();
    const bool erasing = m_session.brushMode() == BrushToolMode::erase;
    title->setText(tool == NavigationTool::spotHealing ? QStringLiteral("Spot Healing") : tool == NavigationTool::cloneStamp ? QStringLiteral("Clone Stamp")
                   : tool == NavigationTool::blur      ? QStringLiteral("Smear") : erasing ? QStringLiteral("Eraser") : QStringLiteral("Brush"));
    m_paint->setVisible(tool == NavigationTool::brush);
    m_erase->setVisible(tool == NavigationTool::brush);
    (erasing ? m_erase : m_paint)->setChecked(true);
    const QList<QAbstractButton *> smears = m_blurModes->buttons();
    for (QAbstractButton *smear : smears)
        smear->setVisible(tool == NavigationTool::blur);
    smears[qsizetype(m_session.blurMode())]->setChecked(true);
    const QList<QAbstractButton *> types = m_healingTypes->buttons();
    for (QAbstractButton *type : types)
        type->setVisible(tool == NavigationTool::spotHealing);
    types[qsizetype(m_session.spotHealingMode())]->setChecked(true);
    const bool cloning = tool == NavigationTool::cloneStamp;
    const CloneSettings &clone = m_session.cloneSettings();
    for (QWidget *widget : std::initializer_list<QWidget *>{m_aligned, m_thisLayer, m_allLayers})
        widget->setVisible(cloning);
    if (m_aligned->isChecked() != clone.aligned)
        m_aligned->setChecked(clone.aligned);
    (clone.sampleAllLayers ? m_allLayers : m_thisLayer)->setChecked(true);
    m_cloneNote->setVisible(cloning && !m_session.cloneSource());
    m_opacityLabel->setText(tool == NavigationTool::blur ? QStringLiteral("Strength") : QStringLiteral("Opacity"));
    m_size->sync(int(std::lround(brush.diameter)));
    m_hardness->sync(int(std::lround(brush.hardness * 100)));
    m_opacity->sync(int(std::lround(brush.opacity * 100)));
    // The sliders show the session's numbers without writing back.
    const QSignalBlocker hardness(m_hardnessSlider), opacity(m_opacitySlider);
    m_hardnessSlider->setValue(int(std::lround(brush.hardness * 1000)));
    m_opacitySlider->setValue(int(std::lround(brush.opacity * 1000)));
    const bool mask = m_session.isMaskSelected();
    m_paintLabel->setVisible(mask);
    m_maskPaint->setVisible(mask);
    m_maskPaint->setCurrentIndex(m_session.maskPaintWhite() ? 1 : 0);
    m_mask->setVisible(mask);
    // Clone Stamp and the Smear paint no colour of theirs.
    const bool colour = !mask && tool != NavigationTool::cloneStamp && tool != NavigationTool::blur;
    m_colourLabel->setVisible(colour);
    m_colour->setVisible(colour);
    m_colour->setEnabled(m_session.canEditPalette());
    m_colour->update();
    setEnabled(!m_session.showsBusy());
}
