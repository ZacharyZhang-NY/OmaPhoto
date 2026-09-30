#include "Document/BrushStroke.h"
#include "ScrubFixtures.h"
#include "UI/CameraRawColorControls.h"
#include "UI/CameraRawControls.h"
#include "UI/CameraRawDetailOpticsControls.h"
#include "UI/CameraRawGeometryCalibrationControls.h"
#include "UI/CameraRawRow.h"
#include "UI/CanvasSizeSheet.h"
#include "UI/ColorPickerSheet.h"
#include "UI/EffectsSheet.h"
#include "UI/FilterSheet.h"
#include "UI/HueSaturationSheet.h"
#include "UI/ImageSizeSheet.h"
#include "UI/LevelsSheet.h"

// A sheet destroyed while its entry holds keys and typing.
namespace {
void paint(EditorSession &session)
{
    session.createDocument(8, 8);
    QImage image = BrushRaster::context(8, 8, false);
    image.fill(QColor(120, 160, 200));
    session.insert(ImportedImage(image, image, QStringLiteral("Colour")));
}

// Names the owner's class as its entry loses the keys.
class FocusWitness : public QObject {
public:
    FocusWitness(QWidget &owner, QWidget &field) : m_owner(owner) { field.installEventFilter(this); }
    QString seen;

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::FocusOut && seen.isEmpty())
            seen = QString::fromLatin1(m_owner.metaObject()->className());
        return false;
    }

private:
    QWidget &m_owner;
};

    // Past its destructor, the owner reads as QWidget.
void typeThenDestroy(std::unique_ptr<QWidget> owner, const QString &typed)
{
    showActive(*owner);
    PickerField *field = owner->findChildren<PickerField *>().value(0);
    if (!field)
        throw std::runtime_error(std::string("no entry in ") + owner->metaObject()->className());
    field->setFocus();
    if (!QTest::qWaitFor([field] { return field->hasFocus(); }))
        throw std::runtime_error("the entry never took the keys");
    field->selectAll();
    QTest::keyClicks(field, typed);
    if (!field->isModified())
        throw std::runtime_error("the typing did not land");
    const QString name = QString::fromLatin1(owner->metaObject()->className());
    FocusWitness witness(*owner, *field);
    owner.reset();
    // Past its own destructor, the owner would read as QWidget.
    if (witness.seen != name)
        throw std::runtime_error("the entry committed into " + witness.seen.toStdString() + ", not " + name.toStdString());
}
}

class SheetTeardownTests : public QObject {
    Q_OBJECT
private slots:
    void theColourSheetsCommitAsTheyGo();
    void theDocumentSheetsGoQuietly();
    void aCameraRawRowGoesQuietly();
    void theCameraRawGroupsGoWhole();
};

void SheetTeardownTests::theColourSheetsCommitAsTheyGo()
{
    // Each commits its typing as hiding would, members alive.
    EditorSession session;
    paint(session);
    session.beginHueSaturation();
    typeThenDestroy(std::make_unique<HueSaturationSheet>(session), QStringLiteral("12"));
    QCOMPARE(session.hueSaturation().value().settings.hue(), 12.0);
    session.cancelHueSaturation();
    session.beginFilter(FilterKind::gaussianBlur);
    typeThenDestroy(std::make_unique<FilterSheet>(session), QStringLiteral("7"));
    QCOMPARE(session.filterEdit().value().settings.radius, 7.0);
    session.cancelFilter();
    session.beginLevels();
    typeThenDestroy(std::make_unique<LevelsSheet>(session), QStringLiteral("9"));
    QCOMPARE(session.levels().value().settings.current().black, 9.0);
    session.cancelLevels();
    session.addEffect(LayerEffectKind::stroke);
    typeThenDestroy(std::make_unique<EffectsSheet>(session, LayerEffectKind::stroke), QStringLiteral("5"));
    QCOMPARE(session.editingEffects().stroke.value().size, 5.0);
    session.openColorPicker(false);
    typeThenDestroy(std::make_unique<ColorPickerSheet>(session, [](bool) {}), QStringLiteral("10"));
    QCOMPARE(session.colorPicker().value().color().red, 10.0 / 255);
}

void SheetTeardownTests::theDocumentSheetsGoQuietly()
{
    const CanvasDocument canvas{120, 80};
    typeThenDestroy(std::make_unique<ImageSizeSheet>(canvas, [](std::optional<ImageSizeOptions>) {}), QStringLiteral("60"));
    typeThenDestroy(std::make_unique<CanvasSizeSheet>(canvas, PaletteColor::black(), PaletteColor::white(), [](std::optional<CanvasSizeOptions>) {}),
                    QStringLiteral("60"));
}

void SheetTeardownTests::aCameraRawRowGoesQuietly()
{
    double typed = 0;
    typeThenDestroy(std::make_unique<CameraRawRow>(CameraRawRow::Spec{.name = QStringLiteral("probe"), .title = QStringLiteral("Probe"), .help = QString()},
                                                   [] { return 0.0; }, [](double) {}, [&typed](double value) { typed = value; }, [] {}),
                    QStringLiteral("30"));
    QCOMPARE(typed, 30.0);
}

void SheetTeardownTests::theCameraRawGroupsGoWhole()
{
    EditorSession session;
    paint(session);
    session.beginFilter(FilterKind::cameraRaw);
    typeThenDestroy(std::make_unique<CameraRawControls>(session), QStringLiteral("1"));
    typeThenDestroy(std::make_unique<CameraRawCurveControls>(session), QStringLiteral("10"));
    typeThenDestroy(std::make_unique<CameraRawMixerControls>(session), QStringLiteral("10"));
    typeThenDestroy(std::make_unique<CameraRawDetailControls>(session), QStringLiteral("10"));
    typeThenDestroy(std::make_unique<CameraRawOpticsControls>(session), QStringLiteral("10"));
    typeThenDestroy(std::make_unique<CameraRawGeometryControls>(session), QStringLiteral("10"));
    typeThenDestroy(std::make_unique<CameraRawCalibrationControls>(session), QStringLiteral("10"));
}

QTEST_MAIN(SheetTeardownTests)
#include "SheetTeardownTests.moc"
