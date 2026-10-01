#include "SessionFixtures.h"
#include "UI/LayerIcons.h"
#include "UI/TransformInspector.h"
#include <QtTest>
#include <cmath>

// The Move tool's bar: fields, settings, Apply and Cancel.
namespace {
struct Bar {
    EditorSession session;
    QUuid layer;
    std::unique_ptr<TransformInspector> header;
    bool active = false;

    Bar()
    {
        session.createDocument(200, 100);
        session.insert(ImportedImage(QImage(40, 20, QImage::Format_RGBA8888_Premultiplied), QImage(), "Image"));
        layer = session.activeLayerID().value();
        session.selectTool(NavigationTool::move);
        header = std::make_unique<TransformInspector>(session);
        header->show();
        header->activateWindow();
        active = QTest::qWaitForWindowActive(header.get());
    }
    QLineEdit &field(const char *name)
    {
        QLineEdit *found = header->findChild<QLineEdit *>(QStringLiteral("transform") + QString::fromUtf8(name));
        if (!found)
            throw std::runtime_error(std::string("no field named ") + name);
        return *found;
    }
    template <typename Widget> Widget &control(const char *name)
    {
        Widget *found = header->findChild<Widget *>(QLatin1String(name));
        if (!found)
            throw std::runtime_error(std::string("no control named ") + name);
        return *found;
    }
    void type(const char *name, const QString &text)
    {
        QLineEdit &edit = field(name);
        edit.setFocus();
        edit.selectAll();
        QTest::keyClicks(&edit, text);
    }
    LayerTransform draft() const { return session.transformEdit().value().draft; }
};
}

class TransformInspectorTests : public QObject {
    Q_OBJECT
private slots:
    void theFieldsShowTheLayerAndTypingPreviews();
    void returnEscapeAndArrowsInAField();
    void settingsButtonsAndTheTitleFollowTheSession();
    void anotherActiveLayerRemakesTheFields();
    void aMenuBorrowsTheFocusAndTheBarsKeysApplyOrCancel();
    void theLockIconFollowsThePalette();
    void theFieldsTakeTheSpareRoom();
    void cancelAndApplyShowOnlyWhileAnEditWaits();
};

void TransformInspectorTests::theFieldsShowTheLayerAndTypingPreviews()
{
    Bar bar;
    QVERIFY(bar.active);
    QCOMPARE(bar.header->title->text(), QString("Transform"));
    QCOMPARE(bar.header->height(), 42);
    QCOMPARE(bar.field("X").text(), QString("80"));
    QCOMPARE(bar.field("Y").text(), QString("40"));
    QCOMPARE(bar.field("W").text(), QString("40"));
    QCOMPARE(bar.field("H").text(), QString("20"));
    QCOMPARE(bar.field("Scale").text(), QString("100"));
    QCOMPARE(bar.field("°").text(), QString("0"));
    QCOMPARE(bar.control<QComboBox>("transformSampling").currentText(), QString("High quality"));
    QCOMPARE(bar.field("X").accessibleName(), QString("X"));
    // Typing begins a Ctrl+T edit and previews it.
    bar.type("X", "90");
    QVERIFY(bar.session.transformEdit().value().persistent);
    QCOMPARE(bar.draft().origin, QPointF(90, 40));
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin, QPointF(80, 40));
    bar.type("Y", "12.5");
    QCOMPARE(bar.draft().origin, QPointF(90, 12.5));
    // Width locked carries the height; unlocked, alone.
    bar.type("W", "80");
    QCOMPARE(bar.draft().size, QSizeF(80, 40));
    QCOMPARE(bar.field("H").text(), QString("40"));
    QCOMPARE(bar.field("Scale").text(), QString("200"));
    bar.control<QToolButton>("locksTransformRatio").click();
    QVERIFY(!bar.session.locksTransformRatio());
    bar.type("W", "20");
    QCOMPARE(bar.draft().size, QSizeF(20, 40));
    bar.type("H", "10");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    // Under a pixel, or no number at all, changes nothing.
    bar.type("H", "0");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    bar.type("H", "abc");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    bar.type("H", "inf");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    // Scale sets both sides about the middle.
    const QPointF center = bar.draft().center();
    bar.type("Scale", "50");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    QCOMPARE(bar.draft().center(), center);
    bar.type("Scale", "0");
    QCOMPARE(bar.draft().size, QSizeF(20, 10));
    bar.type("°", "405");
    QCOMPARE(bar.draft().rotation, 45.0);
    bar.control<QComboBox>("transformSampling").setCurrentIndex(0);
    emit bar.control<QComboBox>("transformSampling").activated(0);
    QCOMPARE(bar.draft().sampling, LayerSampling::nearest);
    bar.control<QPushButton>("flipHorizontal").click();
    QVERIFY(bar.draft().flipX);
    bar.control<QPushButton>("flipVertical").click();
    QVERIFY(bar.draft().flipY);
    // Apply commits the edit as one step; Cancel drops it.
    const LayerTransform drafted = bar.draft();
    bar.control<QPushButton>("applyTransform").click();
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform, drafted);
    QCOMPARE(bar.session.history.undoName(), QString("Transform Layer"));
    bar.type("X", "5");
    QCOMPARE(bar.draft().origin.x(), 5.0);
    bar.control<QPushButton>("cancelTransform").click();
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform, drafted);
}

void TransformInspectorTests::returnEscapeAndArrowsInAField()
{
    Bar bar;
    QVERIFY(bar.active);
    QLineEdit &x = bar.field("X");
    x.setFocus();
    QVERIFY(x.hasFocus());
    // Up and Down step one, Shift ten, writing the number.
    QTest::keyClick(&x, Qt::Key_Up);
    QCOMPARE(bar.draft().origin.x(), 81.0);
    QCOMPARE(x.text(), QString("81"));
    QTest::keyClick(&x, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(bar.draft().origin.x(), 71.0);
    QCOMPARE(x.text(), QString("71"));
    // Return hands the canvas the keys; the edit waits there.
    const int request = bar.session.canvasFocusRequest();
    QTest::keyClick(&x, Qt::Key_Return);
    QVERIFY(!x.hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), request + 1);
    QVERIFY(bar.session.transformEdit().has_value());
    x.setFocus();
    QTest::keyClicks(&x, "2");
    QCOMPARE(x.text(), QString("712"));
    QCOMPARE(bar.draft().origin.x(), 712.0);
    QTest::keyClick(&x, Qt::Key_Escape);
    QVERIFY(!x.hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), request + 2);
    QCOMPARE(bar.draft().origin.x(), 712.0);
    // While a field is typed in, the numbers wait outside.
    x.setFocus();
    LayerTransform moved = bar.draft();
    moved.origin.setX(30);
    bar.session.previewTransform(moved);
    QCOMPARE(x.text(), QString("712"));
    QCOMPARE(bar.field("Y").text(), QString("40"));
    x.clearFocus();
    QCOMPARE(x.text(), QString("30"));
}

void TransformInspectorTests::settingsButtonsAndTheTitleFollowTheSession()
{
    Bar bar;
    QCheckBox &autoSelect = bar.control<QCheckBox>("transformAutoSelect");
    QCheckBox &controls = bar.control<QCheckBox>("showTransformControls");
    QVERIFY(!autoSelect.isChecked() && controls.isChecked());
    QCOMPARE(autoSelect.toolTip(), QString("Select layers by clicking the canvas. When off, hold Ctrl to select a layer."));
    autoSelect.click();
    QVERIFY(bar.session.transformAutoSelect());
    controls.click();
    QVERIFY(!bar.session.showsTransformControls());
    bar.session.setShowsTransformControls(true);
    QVERIFY(controls.isChecked());
    bar.session.setTransformAutoSelect(false);
    QVERIFY(!autoSelect.isChecked());
    QVERIFY(bar.control<QToolButton>("locksTransformRatio").isChecked());
    bar.session.setLocksTransformRatio(false);
    QVERIFY(!bar.control<QToolButton>("locksTransformRatio").isChecked());
    // Apply and Cancel need an edit; the numbers, a layer.
    QPushButton &apply = bar.control<QPushButton>("applyTransform");
    QPushButton &cancel = bar.control<QPushButton>("cancelTransform");
    QWidget &numbers = bar.control<QWidget>("transformFields");
    QVERIFY(!apply.isEnabled() && !cancel.isEnabled() && numbers.isEnabled());
    bar.session.beginTransform();
    QVERIFY(apply.isEnabled() && cancel.isEnabled() && numbers.isEnabled());
    bar.session.cancelTransform();
    bar.session.addBlankLayer();
    QVERIFY(!numbers.isEnabled());
    QCOMPARE(bar.field("W").text(), QString("200"));
    bar.session.setIsProjectBusy(true);
    QVERIFY(!numbers.isEnabled());
    bar.session.setIsProjectBusy(false);
    // An unlinked mask, selected, is what the bar transforms.
    bar.session.selectLayer(bar.layer);
    rewrite(bar.session, [&](ProjectSnapshot &snapshot) {
        setMask(snapshot, bar.layer, coverage());
        record(snapshot, bar.layer).maskLinked = false;
        record(snapshot, bar.layer).maskPlacement = LayerTransform{.origin = {1, 2}, .size = {3, 4}};
    });
    bar.session.selectLayerTarget(bar.layer, true);
    QVERIFY(bar.session.transformTargetsMask());
    QCOMPARE(bar.header->title->text(), QString("Transform Mask"));
    QCOMPARE(bar.field("X").text(), QString("1"));
    QCOMPARE(bar.field("W").text(), QString("3"));
    bar.session.selectLayerTarget(bar.layer, false);
    QCOMPARE(bar.header->title->text(), QString("Transform"));
}

void TransformInspectorTests::anotherActiveLayerRemakesTheFields()
{
    Bar bar;
    QVERIFY(bar.active);
    bar.session.insert(ImportedImage(QImage(10, 10, QImage::Format_RGBA8888_Premultiplied), QImage(), "Other"), QPointF(5, 5));
    const QUuid other = bar.session.activeLayerID().value();
    bar.session.selectLayer(bar.layer);
    QLineEdit &x = bar.field("X");
    x.setFocus();
    QTest::keyClicks(&x, "1");
    QCOMPARE(bar.draft().origin.x(), 801.0);
    // Another layer: the typing ends, the numbers are its.
    bar.session.selectLayer(other);
    QVERIFY(!x.hasFocus());
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(x.text(), QString("0"));
    QCOMPARE(bar.field("W").text(), QString("10"));
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 801.0);
}

void TransformInspectorTests::aMenuBorrowsTheFocusAndTheBarsKeysApplyOrCancel()
{
    Bar bar;
    QVERIFY(bar.active);
    QLineEdit &x = bar.field("X");
    bar.type("X", "12.");
    QCOMPARE(bar.draft().origin.x(), 12.0);
    // The menu bar borrows the focus: the typing stays.
    QPushButton &apply = bar.control<QPushButton>("applyTransform");
    apply.setFocus(Qt::MenuBarFocusReason);
    QVERIFY(!x.hasFocus());
    QCOMPARE(x.text(), QString("12."));
    LayerTransform moved = bar.draft();
    moved.origin.setX(30);
    bar.session.previewTransform(moved);
    QCOMPARE(x.text(), QString("12."));
    x.setFocus();
    QTest::keyClicks(&x, "5");
    QCOMPARE(x.text(), QString("12.5"));
    QCOMPARE(bar.draft().origin.x(), 12.5);
    // A popup borrows it too; leaving otherwise puts it back.
    apply.setFocus(Qt::PopupFocusReason);
    QCOMPARE(x.text(), QString("12.5"));
    x.setFocus();
    apply.setFocus(Qt::TabFocusReason);
    QCOMPARE(x.text(), QString("12.50"));
    // Return on Apply applies; Escape on a toggle cancels.
    apply.setFocus();
    QVERIFY(apply.hasFocus());
    QTest::keyClick(&apply, Qt::Key_Return);
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 12.5);
    bar.type("Y", "7");
    QCOMPARE(bar.draft().origin.y(), 7.0);
    QCheckBox &controls = bar.control<QCheckBox>("showTransformControls");
    controls.setFocus();
    QTest::keyClick(&controls, Qt::Key_Escape);
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.y(), 40.0);
    // Without an edit the keys are the controls' own.
    QTest::keyClick(&controls, Qt::Key_Return);
    QTest::keyClick(&controls, Qt::Key_Escape);
    QVERIFY(!bar.session.transformEdit().has_value());
    QVERIFY(bar.session.showsTransformControls());
}

void TransformInspectorTests::theLockIconFollowsThePalette()
{
    Bar bar;
    QToolButton &lock = bar.control<QToolButton>("locksTransformRatio");
    const double ratio = bar.header->devicePixelRatio();
    QCOMPARE(lock.icon().pixmap(16).toImage(), LayerIcons::pixmap(LayerIcon::link, 16, bar.header->palette().color(QPalette::Text), ratio).toImage());
    QPalette changed = bar.header->palette();
    changed.setColor(QPalette::Text, QColor(200, 30, 30));
    bar.header->setPalette(changed);
    QCoreApplication::processEvents();
    QCOMPARE(lock.icon().pixmap(16).toImage(), LayerIcons::pixmap(LayerIcon::link, 16, QColor(200, 30, 30), ratio).toImage());
}

void TransformInspectorTests::theFieldsTakeTheSpareRoom()
{
    Bar bar;
    QVERIFY(bar.active);
    auto &flip = bar.control<QPushButton>("flipVertical");
    const QRect natural = flip.geometry();
    // Swift's ScrollView takes the room; its content keeps its size.
    bar.header->resize(1600, bar.header->height());
    QTRY_VERIFY(bar.control<QWidget>("pendingTransform").geometry().right() > 1600 - 40);
    QCOMPARE(flip.geometry(), natural);
    QCOMPARE(flip.width(), flip.sizeHint().width());
}

void TransformInspectorTests::cancelAndApplyShowOnlyWhileAnEditWaits()
{
    Bar bar;
    QVERIFY(bar.active);
    QWidget &buttons = bar.control<QWidget>("pendingTransform");
    auto *fade = qobject_cast<QGraphicsOpacityEffect *>(buttons.graphicsEffect());
    QVERIFY(fade);
    QPushButton &apply = bar.control<QPushButton>("applyTransform");
    QPushButton &cancel = bar.control<QPushButton>("cancelTransform");
    const auto seen = [&](bool shown) {
        return fade->opacity() == (shown ? 1.0 : 0.0) && buttons.testAttribute(Qt::WA_TransparentForMouseEvents) == !shown
            && apply.focusPolicy() == (shown ? Qt::StrongFocus : Qt::NoFocus) && cancel.focusPolicy() == apply.focusPolicy();
    };
    QVERIFY(seen(false));
    // SwiftUI's easeOut over 0.12 seconds; 12 points apart.
    auto *fadeIn = bar.header->findChild<QPropertyAnimation *>();
    QVERIFY(fadeIn && fadeIn->duration() == 120 && fadeIn->easingCurve().type() == QEasingCurve::BezierSpline);
    // CSS's ease-out, 0, 0, 0.58, 1: about 0.685 halfway.
    QVERIFY(std::abs(fadeIn->easingCurve().valueForProgress(0.5) - 0.685) < 0.01);
    QCOMPARE(apply.geometry().left() - cancel.geometry().right() - 1, 12);
    // A handle drag applies itself: nothing waits.
    bar.session.beginTransform(false);
    QVERIFY(apply.isEnabled());
    QTest::qWait(200);
    QVERIFY(seen(false));
    // Unseen, the click falls through; Return still applies.
    const QPoint middle = apply.mapTo(bar.header.get(), apply.rect().center());
    QTest::mouseClick(bar.header->windowHandle(), Qt::LeftButton, Qt::NoModifier, middle);
    QVERIFY(bar.session.transformEdit().has_value());
    QTest::keyClick(bar.header.get(), Qt::Key_Return);
    QVERIFY(!bar.session.transformEdit().has_value());
    // Typed values wait: the buttons fade in, a click applies.
    bar.session.beginTransform();
    QVERIFY(fade->opacity() < 1);
    QTRY_VERIFY(seen(true));
    QVERIFY(apply.isVisible() && apply.isEnabled());
    QTest::mouseClick(bar.header->windowHandle(), Qt::LeftButton, Qt::NoModifier, middle);
    QVERIFY(!bar.session.transformEdit().has_value());
    QTRY_VERIFY(seen(false));
    // Waiting again, Cancel drops it.
    bar.session.beginTransform();
    QTRY_VERIFY(seen(true));
    QTest::mouseClick(bar.header->windowHandle(), Qt::LeftButton, Qt::NoModifier, cancel.mapTo(bar.header.get(), cancel.rect().center()));
    QVERIFY(!bar.session.transformEdit().has_value());
    QTRY_VERIFY(seen(false));
}

QTEST_MAIN(TransformInspectorTests)
#include "TransformInspectorTests.moc"
