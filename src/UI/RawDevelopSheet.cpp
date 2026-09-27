#include "UI/RawDevelopSheet.h"
#include "IO/ImageExporter.h"
#include "IO/ImageImporter.h"
#include "Logging.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

// Swift's ZStack: 35% black, the fitted preview, rounded 6.
class RawDevelopSheet::Preview : public QWidget {
public:
    using QWidget::QWidget;
    QImage image;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        QPainterPath plate;
        plate.addRoundedRect(QRectF(rect()), 6, 6);
        painter.fillPath(plate, QColor::fromRgbF(0, 0, 0, 0.35f));
        if (image.isNull())
            return;
        QRectF target(QPointF(), QSizeF(image.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio));
        target.moveCenter(QRectF(rect()).center());
        QPainterPath clip;
        clip.addRoundedRect(target, 6, 6);
        painter.setClipPath(clip);
        painter.drawImage(target, image);
    }
};

RawDevelopSheet::RawDevelopSheet(RawDevelopRequest request, std::function<void(std::optional<RawDevelopSettings>)> finish, QWidget *parent)
    : QWidget(parent), m_path(std::move(request.path)), m_finish(std::move(finish)), m_settings(request.settings), m_wait(new QTimer(this)),
      m_preview(new Preview(this)), m_spinner(new QProgressBar(m_preview)), m_reset(new QPushButton(QStringLiteral("Reset"), this))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    column->setSizeConstraint(QLayout::SetFixedSize);
    auto *title = new QLabel(QStringLiteral("Develop “%1”").arg(QFileInfo(m_path).fileName()), this);
    title->setTextFormat(Qt::PlainText);
    QFont font = title->font();
    font.setPixelSize(17);
    font.setBold(true);
    title->setFont(font);
    column->addWidget(title);
    m_preview->setObjectName(QStringLiteral("rawPreview"));
    m_preview->setFixedSize(560, 340);
    m_spinner->setObjectName(QStringLiteral("rawSpinner"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setGeometry(272, 162, 16, 16);
    column->addWidget(m_preview);

    // Swift's sliders are continuous; these step finer than shown.
    addRow(QStringLiteral("Exposure"), &RawDevelopSettings::exposure, -3, 3, 100, 2, QStringLiteral(" EV"));
    addRow(QStringLiteral("Temperature"), &RawDevelopSettings::temperature, 2000, 12000, 1, 0, QStringLiteral(" K"));
    addRow(QStringLiteral("Tint"), &RawDevelopSettings::tint, -150, 150, 1, 0, QString());
    addRow(QStringLiteral("Boost"), &RawDevelopSettings::boost, 0, 1, 1000, 2, QString());
    for (const Row &row : m_rows) {
        auto *line = new QHBoxLayout;
        line->setSpacing(10);
        auto *label = new QLabel(row.slider->accessibleName(), this);
        label->setFixedWidth(90);
        label->setBuddy(row.slider);
        line->addWidget(label);
        line->addWidget(row.slider);
        line->addWidget(row.value);
        column->addLayout(line);
    }

    auto *buttons = new QHBoxLayout;
    m_reset->setObjectName(QStringLiteral("rawReset"));
    m_reset->setAutoDefault(false);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        m_settings.reset();
        changed();
    });
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("rawCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] { m_finish(std::nullopt); });
    auto *importButton = new QPushButton(QStringLiteral("Import"), this);
    importButton->setObjectName(QStringLiteral("rawImport"));
    importButton->setDefault(true);
    connect(importButton, &QPushButton::clicked, this, [this] { m_finish(m_settings); });
    buttons->addWidget(m_reset);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(importButton);
    column->addLayout(buttons);

    // The first preview starts at once; slider bursts coalesce.
    m_wait->setObjectName(QStringLiteral("rawWait"));
    m_wait->setSingleShot(true);
    m_wait->setTimerType(Qt::PreciseTimer);
    connect(m_wait, &QTimer::timeout, this, &RawDevelopSheet::develop);
    m_wait->setInterval(60);
    QTimer::singleShot(0, this, &RawDevelopSheet::develop);
    synchronize();
}

// SwiftUI cancels the task when the sheet goes.
RawDevelopSheet::~RawDevelopSheet()
{
    if (m_cancelled)
        m_cancelled->store(true);
}

void RawDevelopSheet::addRow(const QString &title, float RawDevelopSettings::*field, double low, double high, double scale, int precision, const QString &unit)
{
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setObjectName(QStringLiteral("raw") + title);
    slider->setAccessibleName(title);
    slider->setFixedWidth(300);
    slider->setRange(int(std::lround(low * scale)), int(std::lround(high * scale)));
    auto *value = new QLabel(this);
    value->setObjectName(slider->objectName() + QStringLiteral("Value"));
    value->setFixedWidth(80);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setForegroundRole(QPalette::PlaceholderText);
    connect(slider, &QSlider::valueChanged, this, [this, field, scale](int step) {
        m_settings.*field = float(step / scale);
        changed();
    });
    m_rows.push_back({slider, value, field, scale, precision, unit});
}

// Swift's onChange: a new revision, developed after 60 ms.
void RawDevelopSheet::changed()
{
    if (m_cancelled)
        m_cancelled->store(true);
    m_wait->start();
    synchronize();
}

void RawDevelopSheet::develop()
{
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_spinner->show();
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, cancelled = m_cancelled] {
        watcher->deleteLater();
        if (cancelled->load())
            return;
        m_spinner->hide();
        // A failed develop keeps the last preview, as Swift's.
        if (const QImage image = watcher->future().takeResult(); !image.isNull()) {
            m_preview->image = image;
            m_preview->update();
        }
    });
    watcher->setFuture(QtConcurrent::run([path = m_path, settings = m_settings] {
        try {
            return RawImporter::Queue::shared().develop(path, settings, 800);
        } catch (const ImageImportError &error) {
            qCWarning(lcIO) << "no RAW preview:" << error.what();
        } catch (const ExportError &error) {
            qCWarning(lcIO) << "no RAW preview:" << error.what();
        }
        return QImage();
    }));
}

void RawDevelopSheet::synchronize()
{
    for (const Row &row : m_rows) {
        const QSignalBlocker blocker(row.slider);
        const double value = m_settings.*row.field;
        row.slider->setValue(int(std::lround(value * row.scale)));
        row.value->setText(QString::asprintf("%.*f", row.precision, value) + row.unit);
    }
    m_reset->setEnabled(!m_settings.isAsShot());
}
