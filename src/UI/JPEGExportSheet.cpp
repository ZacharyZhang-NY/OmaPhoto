#include "UI/JPEGExportSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ByteCounts.h"
#include "UI/ColorPaletteControls.h"
#include <QColorDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

const QString JPEGExportSheet::qualityKey = QStringLiteral("jpegExportQuality");

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

QString grouped(qint64 value)
{
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value);
}

// Swift reads a finite number alone, clamped to 0–1.
double startQuality()
{
    bool number = false;
    const double saved = QSettings().value(JPEGExportSheet::qualityKey).toDouble(&number);
    return number && std::isfinite(saved) ? std::clamp(saved, 0.0, 1.0) : JPEGOptions().quality;
}

struct Encoded {
    std::optional<JPEGResult> result;
    std::optional<QString> failure;
};
}

// Swift's ZStack: dark gray, the fitted preview, a plate.
class JPEGExportSheet::Preview : public QWidget {
public:
    using QWidget::QWidget;
    QImage image;
    bool updating = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor::fromRgbF(0.12f, 0.12f, 0.12f));
        if (!image.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            QRectF target(QPointF(), QSizeF(image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio));
            target.moveCenter(QRectF(rect()).center());
            painter.drawImage(target, image);
        }
        if (updating) {
            // Swift's regular material behind the spinner.
            QColor material = palette().color(QPalette::Window);
            material.setAlphaF(0.85f);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(material);
            QRectF plate(0, 0, 64, 64);
            plate.moveCenter(QRectF(rect()).center());
            painter.drawRoundedRect(plate, 8, 8);
        }
    }
};

JPEGExportSheet::JPEGExportSheet(ExportRaster raster, std::function<void(std::optional<QByteArray>)> finish, QWidget *parent)
    : QWidget(parent), m_raster(std::move(raster)), m_finish(std::move(finish)), m_wait(new QTimer(this)), m_preview(new Preview(this)),
      m_spinner(new QProgressBar(m_preview)), m_quality(new QSlider(Qt::Horizontal, this)),
      m_percent(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)), m_matteSwatch(new SwatchButton([this] { return PaletteColor{m_options.red, m_options.green, m_options.blue}; }, 3, 0, 0.5, this)),
      m_failure(text(QString(), 13, QFont::Normal, QPalette::BrightText, this)), m_bytes(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)),
      m_note(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)), m_export(new QPushButton(QStringLiteral("Export…"), this))
{
    m_options.quality = startQuality();
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    column->addWidget(text(QStringLiteral("Export JPEG"), 17, QFont::Bold, QPalette::WindowText, this));
    m_preview->setObjectName(QStringLiteral("jpegPreview"));
    m_preview->setFixedSize(560, 330);
    m_spinner->setObjectName(QStringLiteral("jpegSpinner"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setGeometry(264, 149, 32, 32);
    column->addWidget(m_preview);

    auto *quality = new QHBoxLayout;
    auto *qualityLabel = new QLabel(QStringLiteral("Quality"), this);
    qualityLabel->setBuddy(m_quality);
    // Swift's slider steps by hundredths.
    m_quality->setObjectName(QStringLiteral("jpegQuality"));
    m_quality->setRange(0, 100);
    m_quality->setValue(int(std::lround(m_options.quality * 100)));
    connect(m_quality, &QSlider::valueChanged, this, [this](int hundredths) {
        m_options.quality = hundredths / 100.0;
        request();
    });
    m_percent->setObjectName(QStringLiteral("jpegPercent"));
    m_percent->setFixedWidth(45);
    m_percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    quality->addWidget(qualityLabel);
    quality->addWidget(m_quality, 1);
    quality->addWidget(m_percent);
    column->addLayout(quality);

    // Swift's ColorPicker: its label and a well, no opacity.
    auto *matte = new QHBoxLayout;
    matte->addWidget(new QLabel(QStringLiteral("Background for transparency"), this));
    m_matteSwatch->setObjectName(QStringLiteral("jpegMatte"));
    m_matteSwatch->setFixedSize(36, 18);
    m_matteSwatch->setAccessibleName(QStringLiteral("Background for transparency"));
    connect(m_matteSwatch, &QAbstractButton::clicked, this, &JPEGExportSheet::pickMatte);
    matte->addWidget(m_matteSwatch);
    matte->addStretch(1);
    column->addLayout(matte);
    column->addWidget(text(QStringLiteral("%1 × %2 px · sRGB").arg(grouped(m_raster.image.width()), grouped(m_raster.image.height())), 13, QFont::Normal,
                           QPalette::PlaceholderText, this));

    auto *buttons = new QHBoxLayout;
    m_failure->setObjectName(QStringLiteral("jpegError"));
    m_bytes->setObjectName(QStringLiteral("jpegBytes"));
    m_note->setObjectName(QStringLiteral("jpegNote"));
    buttons->addWidget(m_failure);
    buttons->addWidget(m_bytes);
    buttons->addWidget(m_note);
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("jpegCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_finish(std::nullopt); });
    m_export->setObjectName(QStringLiteral("jpegExport"));
    m_export->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_export, cancel);
    // Export rests until a result is ready.
    connect(m_export, &QPushButton::clicked, this, [this] {
        QSettings().setValue(qualityKey, m_options.quality);
        m_finish(m_result.value().data);
    });
    buttons->addWidget(cancel);
    buttons->addWidget(m_export);
    column->addLayout(buttons);

    m_wait->setObjectName(QStringLiteral("jpegWait"));
    m_wait->setSingleShot(true);
    m_wait->setInterval(200);
    m_wait->setTimerType(Qt::PreciseTimer);
    connect(m_wait, &QTimer::timeout, this, &JPEGExportSheet::encode);
    request();
}

// SwiftUI cancels the task when the sheet goes.
JPEGExportSheet::~JPEGExportSheet()
{
    m_cancelled->store(true);
}

// Swift's task(id:): the last request stops; this waits 200 ms.
void JPEGExportSheet::request()
{
    if (m_cancelled)
        m_cancelled->store(true);
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_error.reset();
    m_wait->start();
    synchronize();
}

void JPEGExportSheet::encode()
{
    const std::shared_ptr<std::atomic_bool> cancelled = m_cancelled;
    const JPEGOptions requested = m_options;
    auto *watcher = new QFutureWatcher<Encoded>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, cancelled, requested] {
        watcher->deleteLater();
        // Swift's check after the await: a newer request supersedes.
        if (cancelled->load())
            return;
        Encoded encoded = watcher->future().takeResult();
        if (encoded.result) {
            m_result = std::move(encoded.result);
            m_readyOptions = requested;
        } else {
            m_error = encoded.failure.value();
        }
        synchronize();
    });
    watcher->setFuture(QtConcurrent::run([raster = m_raster, requested, cancelled]() -> Encoded {
        try {
            return {ImageExporter::jpeg(raster, requested, [cancelled] { return cancelled->load(); }), std::nullopt};
        } catch (const CancellationError &) {
            return {};
        } catch (const ExportError &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}

// The desktop's colour dialog, SwiftUI's colour panel, without alpha.
void JPEGExportSheet::pickMatte()
{
    auto *dialog = new QColorDialog(QColor::fromRgbF(float(m_options.red), float(m_options.green), float(m_options.blue)), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Background for transparency"));
    connect(dialog, &QColorDialog::colorSelected, this, [this](const QColor &picked) {
        const QColor srgb = picked.toRgb();
        JPEGOptions next = m_options;
        next.red = srgb.redF();
        next.green = srgb.greenF();
        next.blue = srgb.blueF();
        // Swift's onChange: the same colour asks nothing.
        if (next == m_options)
            return;
        m_options = next;
        m_matteSwatch->update();
        request();
    });
    dialog->open();
}

void JPEGExportSheet::synchronize()
{
    const bool ready = m_result && m_readyOptions == m_options;
    const bool updating = m_readyOptions != m_options && !m_error;
    m_preview->image = m_result ? m_result->preview : QImage();
    m_preview->updating = updating;
    m_preview->update();
    m_spinner->setVisible(updating);
    m_percent->setText(QStringLiteral("%1%").arg(std::lround(m_options.quality * 100)));
    m_failure->setVisible(m_error.has_value());
    m_failure->setText(m_error.value_or(QString()));
    m_bytes->setVisible(ready && !m_error);
    m_bytes->setText(m_result ? ByteCounts::file(m_result->data.size()) : QString());
    m_note->setVisible(!m_error);
    m_note->setText(ready ? QStringLiteral("· encoded preview, fitted to window") : QStringLiteral("Updating preview…"));
    m_export->setEnabled(ready && !m_error);
}
