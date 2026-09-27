#include "Document/BrushStroke.h"
#include "Document/EditorSession.h"
#include "Document/ObjectSelection.h"
#include "Document/SubjectRemoval.h"
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

// The model comes from data folders; failures say why.
class SubjectRemovalModelTests : public QObject {
    Q_OBJECT
private slots:
    void aModelThatFailsIsNamedAndTheNextUseLoadsAgain();
};

namespace {
QString failure(const QImage &image)
{
    try {
        SubjectRemoval::run(image, FilterSettings());
    } catch (const SubjectRemovalError &error) {
        return error.kind == SubjectRemovalError::Kind::model ? QString::fromUtf8(error.what()) : QStringLiteral("no subject");
    }
    return QString();
}
}

void SubjectRemovalModelTests::aModelThatFailsIsNamedAndTheNextUseLoadsAgain()
{
    QImage image = BrushRaster::context(200, 150, false);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setBrush(QColor(217, 26, 26));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(100, 75), 40, 40);
    painter.end();
    const QByteArray folders = qgetenv("XDG_DATA_DIRS");
    // No model in the file: ONNX Runtime's words pass on.
    const QTemporaryDir broken;
    QVERIFY(QDir(broken.path()).mkpath(QStringLiteral("omaphoto")));
    QFile garbage(broken.filePath(QStringLiteral("omaphoto/u2net.onnx")));
    QVERIFY(garbage.open(QIODevice::WriteOnly) && garbage.write("not a model") == 11);
    garbage.close();
    qputenv("XDG_DATA_DIRS", broken.path().toUtf8());
    QString said = failure(image);
    QVERIFY2(said.startsWith(QStringLiteral("Remove Background could not use its model: ")) && said.size() > 60, qPrintable(said));
    // No file at all: every folder looked in is named.
    const QTemporaryDir empty;
    qputenv("XDG_DATA_DIRS", empty.path().toUtf8());
    said = failure(image);
    QVERIFY2(said.startsWith(QStringLiteral("Remove Background could not use its model: omaphoto/u2net.onnx is in no data folder (")), qPrintable(said));
    QVERIFY2(said.contains(empty.path()), qPrintable(said));
    // Object mode passes the model's failure on unchanged.
    try {
        ObjectSelection::select(image, QPointF(100, 75), 0, true);
        QFAIL("the object selection ran without a model");
    } catch (const SubjectRemovalError &error) {
        QVERIFY(error.kind == SubjectRemovalError::Kind::model);
    }
    // The session reports it and keeps the selection.
    EditorSession session;
    session.createDocument(200, 150);
    session.insert(ImportedImage(image, image, QStringLiteral("Disc")));
    session.selectAll();
    bool done = false;
    session.selectObject(QPointF(100, 75), SelectionMode::replace, [&done] { done = true; });
    QVERIFY(QTest::qWaitFor([&done] { return done; }, 20000));
    QVERIFY(session.brushError().value().startsWith(QStringLiteral("Remove Background could not use its model: ")));
    QCOMPARE(session.selection().value().path.boundingRect(), QRectF(0, 0, 200, 150));
    // Back in its folder, the next use loads it.
    if (folders.isEmpty())
        qunsetenv("XDG_DATA_DIRS");
    else
        qputenv("XDG_DATA_DIRS", folders);
    QVERIFY(failure(image).isEmpty());
}

QTEST_GUILESS_MAIN(SubjectRemovalModelTests)
#include "SubjectRemovalModelTests.moc"
