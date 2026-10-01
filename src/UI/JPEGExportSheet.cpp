#include "UI/JPEGExportSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ByteCounts.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/LayerIcons.h"
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
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

JPEGExportSheet::JPEGExportSheet(ExportRaster raster, EditorSession &session, std::function<void(std::optional<QByteArray>)> finish, QWidget *parent)
    : QWidget(parent), m_raster(std::move(raster)), m_session(session), m_finish(std::move(finish)), m_wait(new QTimer(this)), m_preview(new JPEGPreview(m_raster.image.size(), [this] { synchronize(); }, this)),
      m_spinner(new QProgressBar(m_preview)), m_quality(new QSlider(Qt::Horizontal, this)),
      m_percent(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)), m_matteSwatch(new DialogColorSwatch(
          QStringLiteral("JPEG Background"), [this] { return PaletteColor{m_options.red, m_options.green, m_options.blue}; },
          [this](const PaletteColor &matte) { setMatte(matte); }, session, this)),
      m_failure(text(QString(), 13, QFont::Normal, QPalette::BrightText, this)), m_bytes(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)),
      m_note(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)), m_export(new QPushButton(QStringLiteral("Export…"), this)),
      m_fit(new QPushButton(QStringLiteral("Fit"), this)), m_zoomIn(new QPushButton(this)), m_zoomOut(new QPushButton(this))
{
    m_options.quality = startQuality();
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    // The title row sits 8 above; it inherits 8.
    auto *top = new QVBoxLayout;
    top->setSpacing(8);
    auto *title = new QHBoxLayout;
    title->addWidget(text(QStringLiteral("Export JPEG"), 17, QFont::Bold, QPalette::WindowText, this));
    title->addStretch(1);
    m_fit->setObjectName(QStringLiteral("jpegFit"));
    m_zoomIn->setObjectName(QStringLiteral("jpegZoomIn"));
    m_zoomIn->setAccessibleName(QStringLiteral("Zoom in"));
    m_zoomOut->setObjectName(QStringLiteral("jpegZoomOut"));
    m_zoomOut->setAccessibleName(QStringLiteral("Zoom out"));
    for (QPushButton *button : {m_fit, m_zoomIn, m_zoomOut}) {
        button->setAutoDefault(false);
        title->addWidget(button);
    }
    connect(m_fit, &QPushButton::clicked, this, [this] { m_preview->setZoom(std::nullopt); });
    connect(m_zoomIn, &QPushButton::clicked, this, [this] { m_preview->zoomBy(1); });
    connect(m_zoomOut, &QPushButton::clicked, this, [this] { m_preview->zoomBy(-1); });
    top->addLayout(title);
    m_preview->setObjectName(QStringLiteral("jpegPreview"));
    m_spinner->setObjectName(QStringLiteral("jpegSpinner"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setGeometry(264, 149, 32, 32);
    top->addWidget(m_preview);
    column->addLayout(top);

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

    // Its label and the app's picker on a swatch.
    auto *matte = new QHBoxLayout;
    matte->setSpacing(8);
    matte->addWidget(new QLabel(QStringLiteral("Background for transparency"), this));
    m_matteSwatch->setObjectName(QStringLiteral("jpegMatte"));
    m_matteSwatch->setToolTip(QStringLiteral("Color that fills transparent areas"));
    matte->addWidget(m_matteSwatch);
    matte->addStretch(1);
    column->addLayout(matte);

    // The size, the result and the buttons share one row.
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    QLabel *size = text(QStringLiteral("%1 × %2 px · sRGB").arg(grouped(m_raster.image.width()), grouped(m_raster.image.height())), 13, QFont::Normal,
                        QPalette::PlaceholderText, this);
    size->setObjectName(QStringLiteral("jpegSize"));
    buttons->addWidget(size);
    buttons->addStretch(1);
    m_failure->setObjectName(QStringLiteral("jpegError"));
    m_bytes->setObjectName(QStringLiteral("jpegBytes"));
    m_note->setObjectName(QStringLiteral("jpegNote"));
    m_note->setText(QStringLiteral("Updating…"));
    buttons->addWidget(m_failure);
    buttons->addWidget(m_bytes);
    buttons->addWidget(m_note);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("jpegCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        m_finish(std::nullopt);
    });
    m_export->setObjectName(QStringLiteral("jpegExport"));
    m_export->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_export, cancel);
    // Export rests until a result is ready.
    connect(m_export, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
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
    bindZoomKeys();
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &JPEGExportSheet::bindZoomKeys);
    request();
}

// A window-modal sheet blocks the menus: it takes their keys.
void JPEGExportSheet::bindZoomKeys()
{
    const std::array<QKeySequence, 4> originals{QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::CTRL | Qt::Key_1),
                                                QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Minus)};
    const std::array<std::function<void()>, 4> actions{[this] { m_preview->setZoom(std::nullopt); }, [this] { m_preview->setZoom(1.0); },
                                                       [this] { m_preview->zoomBy(1); }, [this] { m_preview->zoomBy(-1); }};
    for (size_t index = 0; index < m_zoomKeys.size(); ++index) {
        if (!m_zoomKeys[index]) {
            m_zoomKeys[index] = new QShortcut(this);
            connect(m_zoomKeys[index], &QShortcut::activated, this, actions[index]);
        }
        m_zoomKeys[index]->setKey(ShortcutSettings::shared().menu(originals[index]));
    }
    synchronize();
}

// SwiftUI cancels the task when the sheet goes.
JPEGExportSheet::~JPEGExportSheet()
{
    m_cancelled->store(true);
    DialogColorSwatch::closePicker(m_session);
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

void JPEGExportSheet::setMatte(const PaletteColor &matte)
{
    JPEGOptions next = m_options;
    next.red = matte.red;
    next.green = matte.green;
    next.blue = matte.blue;
    // Equal options ask nothing, as Swift's task id.
    if (next == m_options)
        return;
    m_options = next;
    request();
}

void JPEGExportSheet::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        synchronize();
}

void JPEGExportSheet::synchronize()
{
    const bool ready = m_result && m_readyOptions == m_options;
    const bool updating = m_readyOptions != m_options && !m_error;
    m_preview->setImage(m_result ? m_result->preview : QImage(), updating);
    m_spinner->setVisible(updating);
    m_percent->setText(QStringLiteral("%1%").arg(std::lround(m_options.quality * 100)));
    m_failure->setVisible(m_error.has_value());
    m_failure->setText(m_error.value_or(QString()));
    m_bytes->setVisible(ready && !m_error);
    m_bytes->setText(m_result ? ByteCounts::file(m_result->data.size()) : QString());
    m_note->setVisible(!ready && !m_error);
    m_export->setEnabled(ready && !m_error);
    // The zoom buttons name their keys and the zoom.
    const double zoom = m_preview->shownZoom();
    const QString percent = QStringLiteral("%1%").arg(std::lround(zoom * 100));
    const auto key = [this](size_t index) { return m_zoomKeys[index] ? m_zoomKeys[index]->key().toString(QKeySequence::NativeText) : QString(); };
    m_fit->setEnabled(m_preview->zoom().has_value());
    m_fit->setToolTip(QStringLiteral("Show the whole image (%1)").arg(key(0)));
    m_zoomIn->setEnabled(JPEGPreview::step(zoom, 1).has_value());
    m_zoomIn->setToolTip(QStringLiteral("Zoom in (%1), now %2. At 100% each pixel of the JPEG is one pixel of the screen, as on the canvas").arg(key(2), percent));
    m_zoomOut->setEnabled(JPEGPreview::step(zoom, -1).has_value());
    m_zoomOut->setToolTip(QStringLiteral("Zoom out (%1), now %2").arg(key(3), percent));
    const QColor ink = palette().color(QPalette::ButtonText);
    m_zoomIn->setIcon(LayerIcons::pixmap(LayerIcon::plusMagnifyingGlass, 16, ink, devicePixelRatioF()));
    m_zoomOut->setIcon(LayerIcons::pixmap(LayerIcon::minusMagnifyingGlass, 16, ink, devicePixelRatioF()));
}
