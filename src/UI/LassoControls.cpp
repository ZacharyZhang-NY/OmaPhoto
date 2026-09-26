#include "UI/LassoControls.h"
#include <QFrame>
#include <QRegularExpressionValidator>
#include <QFocusEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

SelectionAmountField::SelectionAmountField(EditorSession &session, int low, int high, std::function<double()> value,
                                           std::function<void(double)> change, QWidget *parent)
    : QLineEdit(parent), m_session(session), m_low(low), m_high(high), m_value(std::move(value)), m_change(std::move(change)), m_amount(low)
{
    setFont(ToolHeaderStyle::controlFont());
    setFixedWidth(40);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Digits only; the bounds clamp, as Swift's binding does.
    setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), this));
    // Typing applies at once; an overflow reads as the top.
    connect(this, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (text.isEmpty())
            return;
        bool fits = false;
        const qlonglong typed = text.toLongLong(&fits);
        m_change(double(fits ? std::clamp<qlonglong>(typed, m_low, m_high) : m_high));
    });
}

void SelectionAmountField::sync(int amount)
{
    m_amount = amount;
    if (!hasFocus() && !m_borrowed)
        setText(QString::number(amount));
}

void SelectionAmountField::apply()
{
    setText(QString::number(m_amount));
}

void SelectionAmountField::keyPressEvent(QKeyEvent *event)
{
    // Return and Escape hand the canvas the keys.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Escape) {
        apply();
        clearFocus();
        m_session.requestCanvasFocus();
        return;
    }
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down) {
        QLineEdit::keyPressEvent(event);
        return;
    }
    const int amount = (event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1) * (event->key() == Qt::Key_Up ? 1 : -1);
    const double stepped = std::clamp(m_value() + amount, double(m_low), double(m_high));
    m_change(stepped);
    setText(QString::number(std::lround(stepped)));
}

void SelectionAmountField::focusOutEvent(QFocusEvent *event)
{
    m_borrowed = event->reason() == Qt::MenuBarFocusReason || event->reason() == Qt::PopupFocusReason;
    if (!m_borrowed)
        apply();
    QLineEdit::focusOutEvent(event);
}

LassoControls::LassoControls(EditorSession &session, QWidget *parent)
    : ToolHeaderBar(QString(), parent), m_session(session), m_marqueeKinds(new QButtonGroup(this)), m_lassoKinds(new QButtonGroup(this)),
      m_modes(new QButtonGroup(this)),
      m_rectangle(choice(m_marqueeKinds, QStringLiteral("marqueeRectangle"), rawValue(LassoKind::rectangle), QStringLiteral("Switch between Rectangle and Ellipse"),
                         [this] { m_session.cancelLasso(); m_session.setMarqueeKind(LassoKind::rectangle); })),
      m_ellipse(choice(m_marqueeKinds, QStringLiteral("marqueeEllipse"), rawValue(LassoKind::ellipse), QStringLiteral("Switch between Rectangle and Ellipse"),
                       [this] { m_session.cancelLasso(); m_session.setMarqueeKind(LassoKind::ellipse); })),
      m_freehand(choice(m_lassoKinds, QStringLiteral("lassoFreehand"), rawValue(LassoKind::freehand), QStringLiteral("Switch between Freehand and Polygonal"),
                        [this] { m_session.cancelLasso(); m_session.setLassoKind(LassoKind::freehand); })),
      m_polygonal(choice(m_lassoKinds, QStringLiteral("lassoPolygonal"), rawValue(LassoKind::polygonal), QStringLiteral("Switch between Freehand and Polygonal"),
                         [this] { m_session.cancelLasso(); m_session.setLassoKind(LassoKind::polygonal); })),
      m_replace(choice(m_modes, QStringLiteral("selectionModeNew"), rawValue(SelectionMode::replace), QStringLiteral("Hold Shift to add or Alt to subtract for one outline"),
                       [this] { m_session.setSelectionModeChoice(SelectionMode::replace); })),
      m_add(choice(m_modes, QStringLiteral("selectionModeAdd"), rawValue(SelectionMode::add), QStringLiteral("Hold Shift to add or Alt to subtract for one outline"),
                   [this] { m_session.setSelectionModeChoice(SelectionMode::add); })),
      m_subtract(choice(m_modes, QStringLiteral("selectionModeSubtract"), rawValue(SelectionMode::subtract), QStringLiteral("Hold Shift to add or Alt to subtract for one outline"),
                        [this] { m_session.setSelectionModeChoice(SelectionMode::subtract); })),
      m_toleranceLabel(new QLabel(QStringLiteral("Tolerance"), this)),
      m_tolerance(new SelectionAmountField(session, 0, 255, [this] { return double(m_session.wandSettings().tolerance); },
                                           [this](double tolerance) { changeWand([tolerance](WandSettings &wand) { wand.tolerance = int(std::lround(tolerance)); }); }, this)),
      m_sampleSize(new QComboBox(this)), m_sources(new QButtonGroup(this)),
      m_thisLayer(choice(m_sources, QStringLiteral("wandThisLayer"), QStringLiteral("This Layer"),
                         QStringLiteral("Read colors from the active layer only, or from every visible layer as shown"),
                         [this] { changeWand([](WandSettings &wand) { wand.sampleAllLayers = false; }); })),
      m_allLayers(choice(m_sources, QStringLiteral("wandAllLayers"), QStringLiteral("All Layers"),
                         QStringLiteral("Read colors from the active layer only, or from every visible layer as shown"),
                         [this] { changeWand([](WandSettings &wand) { wand.sampleAllLayers = true; }); })),
      m_contiguous(new QCheckBox(QStringLiteral("Contiguous"), this)),
      m_antialias(new QCheckBox(QStringLiteral("Anti-alias"), this)), m_expand(new QPushButton(QStringLiteral("Expand"), this)),
      m_expandAmount(new SelectionAmountField(session, 1, 500, [this] { return double(m_session.selectionExpandAmount()); },
                                              [this](double amount) { m_session.setSelectionExpandAmount(int(std::lround(amount))); }, this)),
      m_contract(new QPushButton(QStringLiteral("Contract"), this)),
      m_contractAmount(new SelectionAmountField(session, 1, 500, [this] { return double(m_session.selectionContractAmount()); },
                                                [this](double amount) { m_session.setSelectionContractAmount(int(std::lround(amount))); }, this)),
      m_empty(new QLabel(QStringLiteral("Empty selection"), this)), m_deselect(new QPushButton(QStringLiteral("Deselect"), this))
{
    m_toleranceLabel->setFont(ToolHeaderStyle::controlFont());
    m_tolerance->setObjectName(QStringLiteral("wandTolerance"));
    m_tolerance->setFixedWidth(44);
    m_tolerance->setToolTip(QStringLiteral("How far each color channel (0–255) can differ from the clicked color and still be selected"));
    m_toleranceLabel->setToolTip(m_tolerance->toolTip());
    m_sampleSize->setObjectName(QStringLiteral("wandSampleSize"));
    m_sampleSize->setToolTip(QStringLiteral("Match the clicked pixel, or the average of the pixels around it"));
    for (const WandSampleSize size : {WandSampleSize::point, WandSampleSize::threeByThree, WandSampleSize::fiveByFive})
        m_sampleSize->addItem(::title(size), int(size));
    connect(m_sampleSize, &QComboBox::activated, this, [this](int index) {
        changeWand([index](WandSettings &wand) { wand.sampleSize = WandSampleSize(index); });
    });
    m_contiguous->setObjectName(QStringLiteral("wandContiguous"));
    m_contiguous->setToolTip(QStringLiteral("Select only similar pixels connected to the one you click; off selects them everywhere"));
    connect(m_contiguous, &QCheckBox::toggled, this, [this](bool on) { changeWand([on](WandSettings &wand) { wand.contiguous = on; }); });
    // Rectangles snap to whole pixels, so smoothing applies elsewhere.
    m_antialias->setObjectName(QStringLiteral("selectionAntialiased"));
    m_antialias->setToolTip(QStringLiteral("Smooth selection edges; turn off for hard pixel edges"));
    connect(m_antialias, &QCheckBox::toggled, this, [this](bool on) { m_session.setSelectionAntialiased(on); });
    auto *divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedHeight(18);
    m_expand->setObjectName(QStringLiteral("expandSelection"));
    m_expand->setToolTip(QStringLiteral("Expand the selection by this many pixels"));
    connect(m_expand, &QPushButton::clicked, this, [this] { m_session.expandSelection(m_session.selectionExpandAmount()); });
    m_expandAmount->setObjectName(QStringLiteral("selectionExpandAmount"));
    m_expandAmount->setToolTip(m_expand->toolTip());
    m_contract->setObjectName(QStringLiteral("contractSelection"));
    m_contract->setToolTip(QStringLiteral("Contract the selection by this many pixels"));
    connect(m_contract, &QPushButton::clicked, this, [this] { m_session.contractSelection(m_session.selectionContractAmount()); });
    m_contractAmount->setObjectName(QStringLiteral("selectionContractAmount"));
    m_contractAmount->setToolTip(m_contract->toolTip());
    m_empty->setObjectName(QStringLiteral("emptySelection"));
    m_empty->setFont(ToolHeaderStyle::controlFont());
    m_empty->setForegroundRole(QPalette::PlaceholderText);
    m_deselect->setObjectName(QStringLiteral("deselect"));
    connect(m_deselect, &QPushButton::clicked, this, [this] { m_session.deselect(); });
    // Swift's `unitSuffix`: a unit sits by each field.
    for (QWidget *widget : std::initializer_list<QWidget *>{m_rectangle, m_ellipse, m_freehand, m_polygonal, m_replace, m_add, m_subtract, m_toleranceLabel,
                                                             m_tolerance, m_sampleSize, m_thisLayer, m_allLayers, m_contiguous, m_antialias,
                                                             divider, m_expand, m_expandAmount, new QLabel(QStringLiteral("px"), this), m_contract, m_contractAmount,
                                                             new QLabel(QStringLiteral("px"), this)})
        row->insertWidget(row->count() - 1, widget);
    row->addWidget(m_empty);
    row->addWidget(m_deselect);
    connect(&m_session, &EditorSession::changed, this, &LassoControls::synchronize);
    synchronize();
}

// One field of the wand's settings changes; the rest stay.
void LassoControls::changeWand(const std::function<void(WandSettings &)> &change)
{
    WandSettings wand = m_session.wandSettings();
    change(wand);
    m_session.setWandSettings(wand);
}

// A segment of Swift's segmented picker: one checkable button.
QToolButton *LassoControls::choice(QButtonGroup *group, const QString &name, const QString &text, const QString &tip, const std::function<void()> &pick)
{
    auto *button = new QToolButton(this);
    button->setObjectName(name);
    button->setText(text);
    button->setToolTip(tip);
    button->setFont(ToolHeaderStyle::controlFont());
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    group->addButton(button);
    connect(button, &QToolButton::clicked, this, [pick] { pick(); });
    return button;
}

void LassoControls::synchronize()
{
    const NavigationTool tool = m_session.tool();
    title->setText(tool == NavigationTool::marquee ? QStringLiteral("Marquee") : tool == NavigationTool::wand ? QStringLiteral("Magic Wand") : QStringLiteral("Lasso"));
    for (QToolButton *button : {m_rectangle, m_ellipse})
        button->setVisible(tool == NavigationTool::marquee);
    for (QToolButton *button : {m_freehand, m_polygonal})
        button->setVisible(tool == NavigationTool::lasso);
    m_rectangle->setChecked(m_session.marqueeKind() == LassoKind::rectangle);
    m_ellipse->setChecked(m_session.marqueeKind() == LassoKind::ellipse);
    m_freehand->setChecked(m_session.lassoKind() == LassoKind::freehand);
    m_polygonal->setChecked(m_session.lassoKind() == LassoKind::polygonal);
    // Held Shift or Alt, or an outline's mode, shows live.
    const SelectionMode mode = m_session.displayedSelectionMode();
    m_replace->setChecked(mode == SelectionMode::replace);
    m_add->setChecked(mode == SelectionMode::add);
    m_subtract->setChecked(mode == SelectionMode::subtract);
    const WandSettings &wand = m_session.wandSettings();
    for (QWidget *widget : std::initializer_list<QWidget *>{m_toleranceLabel, m_tolerance, m_sampleSize, m_thisLayer, m_allLayers, m_contiguous})
        widget->setVisible(tool == NavigationTool::wand);
    m_tolerance->sync(wand.tolerance);
    m_sampleSize->setCurrentIndex(int(wand.sampleSize));
    m_thisLayer->setChecked(!wand.sampleAllLayers);
    m_allLayers->setChecked(wand.sampleAllLayers);
    if (m_contiguous->isChecked() != wand.contiguous)
        m_contiguous->setChecked(wand.contiguous);
    m_antialias->setVisible(tool == NavigationTool::lasso || tool == NavigationTool::wand || (tool == NavigationTool::marquee && m_session.marqueeKind() == LassoKind::ellipse));
    if (m_antialias->isChecked() != m_session.selectionAntialiased())
        m_antialias->setChecked(m_session.selectionAntialiased());
    const bool modifies = m_session.canModifySelection();
    for (QWidget *widget : std::initializer_list<QWidget *>{m_expand, m_expandAmount, m_contract, m_contractAmount})
        widget->setEnabled(modifies);
    m_expandAmount->sync(m_session.selectionExpandAmount());
    m_contractAmount->sync(m_session.selectionContractAmount());
    const std::optional<DocumentSelection> selection = m_session.selection();
    m_empty->setVisible(selection && selection->isEmpty());
    m_deselect->setVisible(selection.has_value());
    m_deselect->setEnabled(m_session.canEditSelection());
    setEnabled(!m_session.showsBusy() && m_session.document().has_value());
}
