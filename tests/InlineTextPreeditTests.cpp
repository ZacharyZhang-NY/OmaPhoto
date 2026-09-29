#include "InlineTextDrawFixtures.h"

// A preedit on screen: underlined, or as its method says.
class InlineTextPreeditTests : public QObject {
    Q_OBJECT
private slots:
    void markedTextIsUnderlined();
    void theInputMethodStylesItsPreedit();
};

void InlineTextPreeditTests::markedTextIsUnderlined()
{
    DrawnText shown;
    beginTextAt(shown.session, QPointF(20, 30));
    QInputMethodEvent composing(QStringLiteral("nn"), {});
    QApplication::sendEvent(shown.canvas, &composing);
    // Below the baseline, where no letter of these reaches.
    const LayerTextStyle style = shown.session.textDraft().value().style;
    const QFontMetricsF metrics(TextLayout::font(style));
    const double baseline = LayerTextStyle::padding + style.lineHeight() - metrics.descent();
    // Short of the caret, which spans the whole line.
    const QRect band = shown.editor().textTransform().mapRect(
        QRectF(LayerTextStyle::padding, baseline + 1, metrics.horizontalAdvance(QStringLiteral("nn")) - 4, metrics.descent() - 2)).toAlignedRect();
    QVERIFY(where(shown.grab().copy(band), reddish).isValid());
    // A format that is none keeps the underline, as Qt's.
    QInputMethodEvent formless(QStringLiteral("nn"), {QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, 2, QVariant())});
    QApplication::sendEvent(shown.canvas, &formless);
    QVERIFY(where(shown.grab().copy(band), reddish).isValid());
    QInputMethodEvent committed;
    committed.setCommitString(QStringLiteral("nn"));
    QApplication::sendEvent(shown.canvas, &committed);
    shown.canvas->clearFocus();
    QVERIFY(!where(shown.grab().copy(band), reddish).isValid());
    // Only the preedit: the letters after it stay bare.
    QTest::keyClicks(shown.canvas, QStringLiteral("xx"));
    QTest::keyClick(shown.canvas, Qt::Key_Home);
    QInputMethodEvent ahead(QStringLiteral("nn"), {});
    QApplication::sendEvent(shown.canvas, &ahead);
    const double nnnn = metrics.horizontalAdvance(QStringLiteral("nnnn"));
    const QRect after = shown.editor().textTransform().mapRect(
        QRectF(LayerTextStyle::padding + nnnn + 2, baseline + 1, metrics.horizontalAdvance(QStringLiteral("xx")) - 4, metrics.descent() - 2)).toAlignedRect();
    QVERIFY(where(shown.grab().copy(band), reddish).isValid());
    QVERIFY(!where(shown.grab().copy(after), reddish).isValid());
}

void InlineTextPreeditTests::theInputMethodStylesItsPreedit()
{
    DrawnText shown;
    shown.canvas->setFocus();
    QTRY_VERIFY(shown.canvas->hasFocus());
    beginTextAt(shown.session, QPointF(20, 30));
    // Its clause's format replaces the underline; its caret hides.
    QTextCharFormat clause;
    clause.setBackground(Qt::yellow);
    QInputMethodEvent composing(QStringLiteral("nn"), {QInputMethodEvent::Attribute(QInputMethodEvent::TextFormat, 0, 2, clause),
                                                       QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 2, 0)});
    QApplication::sendEvent(shown.canvas, &composing);
    const LayerTextStyle style = shown.session.textDraft().value().style;
    const QFontMetricsF metrics(TextLayout::font(style));
    const double padding = LayerTextStyle::padding, baseline = padding + style.lineHeight() - metrics.descent();
    const double width = metrics.horizontalAdvance(QStringLiteral("nn"));
    const QPoint above = shown.editor().textTransform().map(QPointF(padding + width / 2, baseline - metrics.xHeight() - 4)).toPoint();
    QCOMPARE(shown.grab().pixelColor(above), QColor(Qt::yellow));
    // Formats count from the preedit, wherever it sits.
    shown.session.cancelText();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("nn"));
    QApplication::sendEvent(shown.canvas, &composing);
    QVERIFY(shown.grab().pixelColor(above) != QColor(Qt::yellow));
    const QPoint later = shown.editor().textTransform().map(QPointF(padding + width * 1.5, baseline - metrics.xHeight() - 4)).toPoint();
    QCOMPARE(shown.grab().pixelColor(later), QColor(Qt::yellow));
    shown.session.cancelText();
    beginTextAt(shown.session, QPointF(20, 30));
    QApplication::sendEvent(shown.canvas, &composing);
    const QRect band = shown.editor().textTransform().mapRect(QRectF(padding, baseline + 1, width - 4, metrics.descent() - 2)).toAlignedRect();
    QVERIFY(!where(shown.grab().copy(band), reddish).isValid());
    const QPoint caret = shown.editor().textTransform().map(QPointF(padding + width + 0.5, padding + style.lineHeight() / 2)).toPoint();
    QVERIFY(shown.editor().caretShown());
    QVERIFY(!reddish(shown.grab().pixelColor(caret)));
    // A caret colour is its own; no format, the underline.
    QInputMethodEvent coloured(QStringLiteral("nn"), {QInputMethodEvent::Attribute(QInputMethodEvent::Cursor, 2, 1, QColor(Qt::blue))});
    QApplication::sendEvent(shown.canvas, &coloured);
    const QColor blue = shown.grab().pixelColor(caret);
    QVERIFY2(blue.blue() > 110 && blue.red() < 90 && blue.green() < 90, qPrintable(blue.name()));
    QVERIFY(where(shown.grab().copy(band), reddish).isValid());
    // The input method's selection shows beside its preedit.
    shown.session.cancelText();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("xx"));
    QInputMethodEvent selecting(QStringLiteral("nn"), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 0, 2, QVariant())});
    QApplication::sendEvent(shown.canvas, &selecting);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("xxnn"));
    const QPalette::ColorGroup group = shown.canvas->hasFocus() ? QPalette::Active : QPalette::Inactive;
    const QPoint first = shown.editor().textTransform().map(QPointF(padding + 3, padding + 3)).toPoint();
    QCOMPARE(shown.grab().pixelColor(first), shown.canvas->palette().color(group, QPalette::Highlight));
    // Past the preedit, it sits after the preedit's letters.
    shown.session.cancelText();
    beginTextAt(shown.session, QPointF(20, 30));
    QTest::keyClicks(shown.canvas, QStringLiteral("xx"));
    QInputMethodEvent ahead(QStringLiteral("nn"), {QInputMethodEvent::Attribute(QInputMethodEvent::Selection, 2, -2, QVariant())});
    QApplication::sendEvent(shown.canvas, &ahead);
    QCOMPARE(shown.session.textDraft().value().style.content, QString("nnxx"));
    const double nn = metrics.horizontalAdvance(QStringLiteral("nn"));
    const QPoint x = shown.editor().textTransform().map(QPointF(padding + nn + 3, padding + 3)).toPoint();
    QCOMPARE(shown.grab().pixelColor(x), shown.canvas->palette().color(group, QPalette::Highlight));
    QVERIFY(shown.grab().pixelColor(first) != shown.canvas->palette().color(group, QPalette::Highlight));
}

QTEST_MAIN(InlineTextPreeditTests)
#include "InlineTextPreeditTests.moc"
