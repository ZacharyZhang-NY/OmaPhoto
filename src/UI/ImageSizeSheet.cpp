#include "UI/ImageSizeSheet.h"
#include "Document/DocumentLimits.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ColorPickerSheet.h"
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}

// Swift's `.number`: three places at most, none trailing, ties even.
QString shown(double value, QLocale locale)
{
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QString number = locale.toString(std::nearbyint(value * 1000) / 1000, 'f', 3);
    while (number.endsWith(locale.zeroDigit()))
        number.chop(locale.zeroDigit().size());
    if (number.endsWith(locale.decimalPoint()))
        number.chop(locale.decimalPoint().size());
    return number;
}

QString grouped(qint64 value)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value);
}

bool physical(const QString &unit)
{
    return unit == QLatin1String("Inches") || unit == QLatin1String("Centimeters");
}
}

ImageSizeSheet::ImageSizeSheet(const CanvasDocument &document, std::function<void(std::optional<ImageSizeOptions>)> finish, QWidget *parent)
    : QWidget(parent), m_originalWidth(document.width), m_originalHeight(document.height), m_finish(std::move(finish)),
      m_width(double(document.width)), m_height(double(document.height)), m_resolution(document.resolution), m_units(new QComboBox(this)),
      m_widthField(dimension(true)), m_heightField(dimension(false)), m_lock(new QCheckBox(QStringLiteral("Lock aspect ratio"), this)),
      m_resolutionField(new PickerField([this] {
          bool number = false;
          const double typed = m_resolutionField->locale().toDouble(m_resolutionField->text(), &number);
          if (m_resolutionField->isModified() && number) {
              // Swift's onChange: a physical size keeps, its pixels scale.
              const double old = std::exchange(m_resolution, typed);
              if (m_resample && physical(m_unit) && old > 0 && typed > 0 && std::isfinite(typed)) {
                  m_width *= typed / old;
                  m_height *= typed / old;
              }
          }
          m_resolutionField->setModified(false);
          synchronize();
      }, nullptr, this)),
      m_samplingRow(new QWidget(this)), m_samplingChoice(new QComboBox(m_samplingRow)),
      m_explanation(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)),
      m_result(text(QString(), 12, QFont::Normal, QPalette::PlaceholderText, this)), m_resize(new QPushButton(QStringLiteral("Resize"), this))
{
    setFixedWidth(430);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(18);
    column->addWidget(text(QStringLiteral("Image Size"), 17, QFont::Bold, QPalette::WindowText, this));
    column->addWidget(text(QStringLiteral("Current: %1 × %2 pixels").arg(grouped(document.width), grouped(document.height)), 13, QFont::Normal,
                           QPalette::PlaceholderText, this));

    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(8);
    fields->setVerticalSpacing(18);
    fields->setColumnMinimumWidth(0, 75);
    auto *units = new QLabel(QStringLiteral("Units"), this);
    units->setBuddy(m_units);
    m_units->setObjectName(QStringLiteral("imageUnits"));
    connect(m_units, &QComboBox::activated, this, [this](int index) {
        // A wheel leaves the focus: pending typing commits first.
        m_units->setFocus();
        m_unit = m_units->itemText(index);
        synchronize();
    });
    fields->addWidget(units, 0, 0);
    fields->addWidget(m_units, 0, 1);
    fields->addWidget(new QLabel(QStringLiteral("Width"), this), 1, 0);
    fields->addWidget(m_widthField, 1, 1);
    fields->addWidget(new QLabel(QStringLiteral("Height"), this), 2, 0);
    fields->addWidget(m_heightField, 2, 1);
    column->addLayout(fields);

    m_lock->setObjectName(QStringLiteral("imageLocked"));
    connect(m_lock, &QCheckBox::clicked, this, [this](bool on) {
        m_locked = on;
        synchronize();
    });
    column->addWidget(m_lock);
    auto *resolution = new QHBoxLayout;
    resolution->addWidget(new QLabel(QStringLiteral("Resolution"), this));
    m_resolutionField->setObjectName(QStringLiteral("imageResolution"));
    m_resolutionField->setPlaceholderText(QStringLiteral("Resolution"));
    m_resolutionField->setAccessibleName(QStringLiteral("Resolution"));
    resolution->addWidget(m_resolutionField, 1);
    resolution->addWidget(text(QStringLiteral("pixels/inch"), 13, QFont::Normal, QPalette::PlaceholderText, this));
    column->addLayout(resolution);
    // Resample changes only by its own box.
    auto *resample = new QCheckBox(QStringLiteral("Resample"), this);
    resample->setObjectName(QStringLiteral("imageResample"));
    resample->setChecked(true);
    connect(resample, &QCheckBox::clicked, this, &ImageSizeSheet::setResample);
    column->addWidget(resample);

    m_samplingRow->setObjectName(QStringLiteral("imageSamplingRow"));
    auto *samplingLayout = new QHBoxLayout(m_samplingRow);
    samplingLayout->setContentsMargins(0, 0, 0, 0);
    auto *sampling = new QLabel(QStringLiteral("Sampling"), m_samplingRow);
    sampling->setBuddy(m_samplingChoice);
    m_samplingChoice->setObjectName(QStringLiteral("imageSampling"));
    for (const LayerSampling each : {LayerSampling::nearest, LayerSampling::smooth, LayerSampling::high})
        m_samplingChoice->addItem(rawValue(each), int(each));
    connect(m_samplingChoice, &QComboBox::activated, this, [this](int index) {
        m_sampling = LayerSampling(m_samplingChoice->itemData(index).toInt());
    });
    samplingLayout->addWidget(sampling);
    samplingLayout->addWidget(m_samplingChoice, 1);
    column->addWidget(m_samplingRow);
    m_explanation->setObjectName(QStringLiteral("imageExplanation"));
    m_explanation->setWordWrap(true);
    column->addWidget(m_explanation);
    m_result->setObjectName(QStringLiteral("imageResult"));
    m_result->setWordWrap(true);
    column->addWidget(m_result);

    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("imageCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_finish(std::nullopt); });
    m_resize->setObjectName(QStringLiteral("imageResize"));
    m_resize->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_resize, cancel);
    // Resize rests while the size is invalid: no guard needed.
    connect(m_resize, &QPushButton::clicked, this, [this] {
        m_finish(ImageSizeOptions{.width = qint64(std::round(m_width)), .height = qint64(std::round(m_height)), .resolution = m_resolution,
                                  .sampling = m_sampling});
    });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_resize);
    column->addLayout(buttons);
    synchronize();
}

// NaN and infinity fail every range test.
bool ImageSizeSheet::valid() const
{
    const double width = std::round(m_width), height = std::round(m_height);
    return m_resolution >= 1 && m_resolution <= 9600 && width >= 1 && width <= DocumentLimits::maxSide && height >= 1 && height <= DocumentLimits::maxSide
        && (!m_resample || width * height <= DocumentLimits::maxSurfacePixels);
}

double ImageSizeSheet::display(double pixels, qint64 original) const
{
    if (m_unit == QLatin1String("Percent"))
        return pixels / double(original) * 100;
    if (m_unit == QLatin1String("Inches"))
        return pixels / m_resolution;
    if (m_unit == QLatin1String("Centimeters"))
        return pixels / m_resolution * 2.54;
    return pixels;
}

PickerField *ImageSizeSheet::dimension(bool isWidth)
{
    auto *field = new PickerField([this, isWidth] {
        PickerField &edited = isWidth ? *m_widthField : *m_heightField;
        bool number = false;
        const double typed = edited.locale().toDouble(edited.text(), &number);
        if (edited.isModified() && number)
            setDimension(typed, isWidth);
        edited.setModified(false);
        synchronize();
    }, nullptr, this);
    field->setObjectName(isWidth ? QStringLiteral("imageWidth") : QStringLiteral("imageHeight"));
    field->setPlaceholderText(isWidth ? QStringLiteral("Width") : QStringLiteral("Height"));
    field->setAccessibleName(isWidth ? QStringLiteral("Width") : QStringLiteral("Height"));
    return field;
}

// Swift's binding: pixels, or without resampling the resolution.
void ImageSizeSheet::setDimension(double value, bool isWidth)
{
    if (!(std::isfinite(value) && value > 0))
        return;
    if (!m_resample) {
        m_resolution = (isWidth ? m_width : m_height) / value * (m_unit == QLatin1String("Centimeters") ? 2.54 : 1);
        return;
    }
    double pixels = value;
    if (m_unit == QLatin1String("Percent"))
        pixels = value / 100 * double(isWidth ? m_originalWidth : m_originalHeight);
    else if (m_unit == QLatin1String("Inches"))
        pixels = value * m_resolution;
    else if (m_unit == QLatin1String("Centimeters"))
        pixels = value / 2.54 * m_resolution;
    if (isWidth) {
        if (m_locked)
            m_height = pixels * m_height / m_width;
        m_width = pixels;
    } else {
        if (m_locked)
            m_width = pixels * m_width / m_height;
        m_height = pixels;
    }
}

// Swift's onChange: without resampling the pixels stay the document's.
void ImageSizeSheet::setResample(bool enabled)
{
    m_resample = enabled;
    if (!enabled) {
        m_width = double(m_originalWidth);
        m_height = double(m_originalHeight);
        m_locked = true;
        if (!physical(m_unit))
            m_unit = QStringLiteral("Inches");
    }
    synchronize();
}

void ImageSizeSheet::synchronize()
{
    // Pixels and Percent need resampling, as Swift's filter.
    QStringList shownUnits = {QStringLiteral("Pixels"), QStringLiteral("Percent"), QStringLiteral("Inches"), QStringLiteral("Centimeters")};
    if (!m_resample)
        shownUnits = {QStringLiteral("Inches"), QStringLiteral("Centimeters")};
    QStringList listed;
    for (int index = 0; index < m_units->count(); ++index)
        listed << m_units->itemText(index);
    if (listed != shownUnits) {
        m_units->clear();
        m_units->addItems(shownUnits);
    }
    m_units->setCurrentText(m_unit);
    for (const auto &[field, isWidth] : {std::pair(m_widthField, true), std::pair(m_heightField, false)}) {
        // A field being typed in keeps its typing.
        const QString number = shown(display(isWidth ? m_width : m_height, isWidth ? m_originalWidth : m_originalHeight), field->locale());
        if (!field->isModified() && field->text() != number)
            field->setText(number);
    }
    const QString resolution = shown(m_resolution, m_resolutionField->locale());
    if (!m_resolutionField->isModified() && m_resolutionField->text() != resolution)
        m_resolutionField->setText(resolution);
    m_lock->setChecked(m_locked);
    m_lock->setEnabled(m_resample);
    m_samplingRow->setVisible(m_resample);
    m_samplingChoice->setCurrentIndex(m_samplingChoice->findData(int(m_sampling)));
    m_explanation->setText(m_resample ? QStringLiteral("Resizes layer pixels and applies existing transforms. Undo restores the originals.")
                                      : QStringLiteral("Only print dimensions and resolution change. Pixels stay unchanged."));
    const bool isValid = valid();
    m_result->setText(isValid ? QStringLiteral("Result: %1 × %2 pixels").arg(grouped(qint64(std::round(m_width))), grouped(qint64(std::round(m_height))))
                              : QStringLiteral("Use 1–%1 pixels per side, up to %2 megapixels, and 1–9,600 pixels/inch.").arg(DocumentLimits::maxSideText()).arg(DocumentLimits::maxSurfaceMegapixels()));
    m_result->setForegroundRole(isValid ? QPalette::PlaceholderText : QPalette::BrightText);
    m_resize->setEnabled(isValid);
}
