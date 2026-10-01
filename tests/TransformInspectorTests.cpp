#include "SessionFixtures.h"
#include "UI/HeldModifiers.h"
#include "UI/LayerIcons.h"
#include "UI/TransformInspector.h"
#include <QtTest>

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
    void heldKeysShowFlippedInTheBar();
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
    // Typing previews in an edit of the fields' own.
    const int base = bar.session.history.undoCount();
    bar.type("X", "90");
    QVERIFY(!bar.session.transformEdit().value().persistent);
    QVERIFY(bar.session.transformEdit().value().fromFields);
    QCOMPARE(bar.draft().origin, QPointF(90, 40));
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin, QPointF(80, 40));
    // Leaving the field applies it, one step.
    bar.type("Y", "12.5");
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin, QPointF(90, 40));
    QCOMPARE(bar.session.history.undoCount(), base + 1);
    QCOMPARE(bar.session.history.undoName(), QString("Transform Layer"));
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
    // Apply commits the open edit; Cancel drops it.
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
    // Return applies and hands the canvas the keys.
    const int request = bar.session.canvasFocusRequest();
    const int steps = bar.session.history.undoCount();
    QTest::keyClick(&x, Qt::Key_Return);
    QVERIFY(!x.hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), request + 1);
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 71.0);
    QCOMPARE(bar.session.history.undoCount(), steps + 1);
    // Escape throws the fields' change away.
    x.setFocus();
    QTest::keyClicks(&x, "2");
    QCOMPARE(x.text(), QString("712"));
    QCOMPARE(bar.draft().origin.x(), 712.0);
    QTest::keyClick(&x, Qt::Key_Escape);
    QVERIFY(!x.hasFocus());
    QCOMPARE(bar.session.canvasFocusRequest(), request + 2);
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 71.0);
    QCOMPARE(x.text(), QString("71"));
    QCOMPARE(bar.session.history.undoCount(), steps + 1);
    // Ctrl+T's edit takes typing and waits through Return and Escape.
    bar.session.beginTransform();
    x.setFocus();
    QTest::keyClicks(&x, "5");
    QCOMPARE(bar.draft().origin.x(), 715.0);
    QVERIFY(!bar.session.transformEdit().value().fromFields);
    QTest::keyClick(&x, Qt::Key_Return);
    QCOMPARE(bar.draft().origin.x(), 715.0);
    x.setFocus();
    QTest::keyClick(&x, Qt::Key_Escape);
    QCOMPARE(bar.draft().origin.x(), 715.0);
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 71.0);
    // While a field is typed in, the numbers wait outside.
    x.setFocus();
    LayerTransform moved = bar.draft();
    moved.origin.setX(30);
    bar.session.previewTransform(moved);
    QCOMPARE(x.text(), QString("715"));
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
    QCOMPARE(autoSelect.toolTip(), QString("Select layers by clicking the canvas. Hold Ctrl to turn it the other way while you click."));
    QCOMPARE(bar.control<QToolButton>("locksTransformRatio").toolTip(),
             QString("Lock aspect ratio. Hold Shift while dragging a handle to turn it the other way."));
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
    // Another window in front keeps it too.
    apply.setFocus(Qt::ActiveWindowFocusReason);
    QCOMPARE(x.text(), QString("12.5"));
    QVERIFY(bar.session.transformEdit().has_value());
    x.setFocus();
    apply.setFocus(Qt::TabFocusReason);
    QCOMPARE(x.text(), QString("12.50"));
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 12.5);
    // Return on Apply applies Ctrl+T's; Escape on a toggle cancels.
    bar.session.beginTransform();
    bar.type("X", "14");
    apply.setFocus();
    QVERIFY(apply.hasFocus());
    QVERIFY(bar.session.transformEdit().has_value());
    QTest::keyClick(&apply, Qt::Key_Return);
    QVERIFY(!bar.session.transformEdit().has_value());
    QCOMPARE(layerWith(bar.session, bar.layer).transform.origin.x(), 14.0);
    bar.session.beginTransform();
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
    QTRY_VERIFY(bar.control<QPushButton>("applyTransform").geometry().right() > 1600 - 40);
    QCOMPARE(flip.geometry(), natural);
    QCOMPARE(flip.width(), flip.sizeHint().width());
}

// Swift's HeldModifiers: Ctrl flips Auto Select, Shift the lock.
void TransformInspectorTests::heldKeysShowFlippedInTheBar()
{
    Bar bar;
    QVERIFY(bar.active);
    QCheckBox &autoSelect = bar.control<QCheckBox>("transformAutoSelect");
    QToolButton &lock = bar.control<QToolButton>("locksTransformRatio");
    HeldModifiers &held = HeldModifiers::shared();
    autoSelect.setFocus();
    QVERIFY(!autoSelect.isChecked() && lock.isChecked());
    // A modifier's own press counts it, whatever the event says.
    QTest::keyPress(&autoSelect, Qt::Key_Control, Qt::NoModifier);
    QCOMPARE(held.flags(), Qt::KeyboardModifiers(Qt::ControlModifier));
    QVERIFY(autoSelect.isChecked());
    QVERIFY(!bar.session.transformAutoSelect());
    // Only a change is announced.
    QSignalSpy announced(&held, &HeldModifiers::changed);
    const auto press = [&](Qt::KeyboardModifiers modifiers) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_A, modifiers);
        QApplication::sendEvent(&autoSelect, &event);
    };
    press(Qt::ControlModifier);
    QCOMPARE(announced.count(), 0);
    press(Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(announced.count(), 1);
    press(Qt::ControlModifier);
    QCOMPARE(announced.count(), 2);
    // A click while held writes the other way.
    autoSelect.click();
    QVERIFY(bar.session.transformAutoSelect());
    QVERIFY(!autoSelect.isChecked());
    QTest::keyRelease(&autoSelect, Qt::Key_Control, Qt::ControlModifier);
    QCOMPARE(held.flags(), Qt::KeyboardModifiers());
    QVERIFY(autoSelect.isChecked());
    // Shift flips the lock likewise; Alt and Meta count, unshown.
    QTest::keyPress(&autoSelect, Qt::Key_Shift, Qt::AltModifier | Qt::MetaModifier | Qt::KeypadModifier);
    QCOMPARE(held.flags(), Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    QVERIFY(!lock.isChecked());
    QVERIFY(autoSelect.isChecked());
    lock.click();
    QVERIFY(!bar.session.locksTransformRatio());
    QVERIFY(lock.isChecked());
    QTest::keyRelease(&autoSelect, Qt::Key_Shift, Qt::ShiftModifier);
    QVERIFY(!lock.isChecked());
    QTest::keyRelease(&autoSelect, Qt::Key_Alt, Qt::AltModifier);
    QTest::keyRelease(&autoSelect, Qt::Key_Meta, Qt::MetaModifier);
    QCOMPARE(held.flags(), Qt::KeyboardModifiers());
    // Left presses and releases read modifiers; others do not.
    QTest::mousePress(bar.header.get(), Qt::LeftButton, Qt::ControlModifier, QPoint(2, 2));
    QCOMPARE(held.flags(), Qt::KeyboardModifiers(Qt::ControlModifier));
    QTest::mouseRelease(bar.header.get(), Qt::RightButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(held.flags(), Qt::KeyboardModifiers(Qt::ControlModifier));
    QTest::mouseRelease(bar.header.get(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QCOMPARE(held.flags(), Qt::KeyboardModifiers());
    // Another app in front lets every key go.
    QTest::keyPress(&autoSelect, Qt::Key_Shift, Qt::ShiftModifier);
    QVERIFY(lock.isChecked());
    emit qApp->applicationStateChanged(Qt::ApplicationInactive);
    QCOMPARE(held.flags(), Qt::KeyboardModifiers());
    QVERIFY(!lock.isChecked());
    // Typing in a field, keys count as none.
    QLineEdit &x = bar.field("X");
    x.setFocus();
    QTest::keyPress(&x, Qt::Key_Control, Qt::ControlModifier);
    QCOMPARE(held.flags(), Qt::KeyboardModifiers());
    QVERIFY(autoSelect.isChecked() == bar.session.transformAutoSelect());
    QTest::keyRelease(&x, Qt::Key_Control, Qt::NoModifier);
}

QTEST_MAIN(TransformInspectorTests)
#include "TransformInspectorTests.moc"
