#include "LayerDragFixtures.h"

// Layer list drops: rows and masks moved or copied.
class LayerDragTests : public QObject {
    Q_OBJECT
private slots:
    void aDragCarriesTheRowOrTheSelectionItBelongsTo();
    void dropsMoveAboveARowIntoAFolderOrToTheBottom();
    void dropsAreRefusedWhereTheyCannotLand();
    void anAltDragDropsDuplicates();
    void aMaskDragCopiesTheMaskOntoTheRowUnderIt();
    void theIndicatorShowsWhereTheDropLands();
    void anotherListsRowsLandNowhere();
    void aDragNearTheEdgeScrollsTheList();
    void aDragHeldAtTheEdgeScrollsOnAndAClipRefreshesTheCursor();
    void leavingOrDroppingAtTheEdgeStopsTheScrolling();
};

void LayerDragTests::aDragCarriesTheRowOrTheSelectionItBelongsTo()
{
    const auto session = sessionWithLayers(3);
    Shown shown(*session);
    session->selectLayers({shown.id(0), shown.id(2)}, shown.id(2));
    const auto ids = [&](const QMimeData &data) { return QString::fromUtf8(data.data(ProjectWorkspace::layerType)).split('\n'); };
    // The selection in list order, or the pressed row alone.
    QCOMPARE(ids(*shown.list.dragData(*shown.list.cells().at(0))), (QStringList{uuidString(shown.id(0)), uuidString(shown.id(2))}));
    QCOMPARE(ids(*shown.list.dragData(*shown.list.cells().at(2))), (QStringList{uuidString(shown.id(0)), uuidString(shown.id(2))}));
    QCOMPARE(ids(*shown.list.dragData(*shown.list.cells().at(1))), QStringList{uuidString(shown.id(1))});
    QVERIFY(shown.list.dragData(*shown.list.cells().at(1))->hasFormat(ProjectWorkspace::layerType));
}

void LayerDragTests::dropsMoveAboveARowIntoAFolderOrToTheBottom()
{
    const auto session = sessionWithLayers(3);
    Shown shown(*session);
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 3", "Layer 2", "Layer 1"}));
    // A plain row's middle: the lower half lands below.
    QVERIFY(shown.drop(shown.rows({2}), Qt::MoveAction | Qt::CopyAction, shown.middle(0)));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 3", "Layer 1", "Layer 2"}));
    const QPoint lower = shown.list.cells().at(2)->mapTo(&shown.list, QPoint(100, LayerCell::rowHeight - 4));
    QVERIFY(shown.drop(shown.rows({0}), Qt::MoveAction, lower));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1", "Layer 2", "Layer 3"}));
    session->undo();
    session->undo();
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 3", "Layer 2", "Layer 1"}));
    const int steps = session->history.undoCount();
    // Dropped above the top row: a move, whatever Ctrl proposes.
    QVERIFY(shown.drop(shown.rows({2}), Qt::MoveAction | Qt::CopyAction, shown.top(0), Qt::ControlModifier));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1", "Layer 3", "Layer 2"}));
    QCOMPARE(shown.lastAction, Qt::MoveAction);
    QCOMPARE(session->history.undoName(), QString("Move Layer"));
    QCOMPARE(session->history.undoCount(), steps + 1);
    QCOMPARE(session->selectedLayerIDs(), QSet<QUuid>{shown.id(0)});
    // Two rows dropped below every row keep their order.
    QVERIFY(shown.drop(shown.rows({0, 1}), Qt::MoveAction | Qt::CopyAction, shown.below()));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 2", "Layer 1", "Layer 3"}));
    QCOMPARE(session->history.undoName(), QString("Move Layers"));
    QCOMPARE(session->selectedLayerIDs(), (QSet<QUuid>{shown.id(1), shown.id(2)}));
    QCOMPARE(session->activeLayerID(), std::optional(shown.id(1)));
    // Into a folder's middle: each lands on top, in order.
    session->selectLayer(shown.id(0));
    session->addGroup();
    QCOMPARE(shown.names().front(), QString("Folder 1"));
    const QUuid folder = shown.id(0);
    QVERIFY(shown.drop(shown.rows({2, 3}), Qt::MoveAction | Qt::CopyAction, shown.middle(0)));
    QCOMPARE(layerWith(*session, shown.id(1)).parentID, std::optional(folder));
    QCOMPARE(layerWith(*session, shown.id(2)).parentID, std::optional(folder));
    QCOMPARE(shown.names(), (std::vector<QString>{"Folder 1", "Layer 1", "Layer 3", "Layer 2"}));
    // Above a child lands inside; the folder's top, outside.
    QVERIFY(shown.drop(shown.rows({3}), Qt::MoveAction | Qt::CopyAction, shown.top(2)));
    QCOMPARE(layerWith(*session, shown.id(2)).parentID, std::optional(folder));
    QCOMPARE(shown.names(), (std::vector<QString>{"Folder 1", "Layer 1", "Layer 2", "Layer 3"}));
    QVERIFY(shown.drop(shown.rows({3}), Qt::MoveAction | Qt::CopyAction, shown.top(0)));
    QVERIFY(!layerWith(*session, shown.id(0)).parentID.has_value());
    QCOMPARE(shown.names().front(), QString("Layer 3"));
    session->undo();
    QCOMPARE(shown.names().front(), QString("Folder 1"));
}

void LayerDragTests::dropsAreRefusedWhereTheyCannotLand()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    session->selectLayer(shown.id(0));
    session->groupSelectedLayers();
    const QUuid folder = shown.id(0);
    // A folder into itself, or into its own child's row.
    QVERIFY(!shown.drop(shown.rows({0}), Qt::MoveAction | Qt::CopyAction, shown.middle(0)));
    QVERIFY(!shown.drop(shown.rows({0, 1}), Qt::MoveAction | Qt::CopyAction, shown.middle(0)));
    QCOMPARE(layerWith(*session, shown.id(1)).parentID, std::optional(folder));
    // Strangers, another type, an empty payload: nothing lands.
    auto *stranger = new QMimeData;
    stranger->setData(ProjectWorkspace::layerType, uuidString(QUuid::createUuid()).toUtf8());
    QVERIFY(!shown.drop(stranger, Qt::MoveAction | Qt::CopyAction, shown.top(2)));
    auto *text = new QMimeData;
    text->setText(uuidString(shown.id(2)));
    QVERIFY(!shown.drop(text, Qt::MoveAction | Qt::CopyAction, shown.top(0)));
    QVERIFY(!shown.drop(shown.rows({}), Qt::MoveAction | Qt::CopyAction, shown.top(0)));
    // A folder's contents ride with it, placed once.
    QVERIFY(shown.drop(shown.rows({0, 1}), Qt::MoveAction | Qt::CopyAction, shown.below()));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1", "Folder 1", "Layer 2"}));
    QCOMPARE(layerWith(*session, shown.id(2)).parentID, std::optional(folder));
    QCOMPARE(session->history.undoName(), QString("Move Layer"));
    // Busy, no drop; a drop changing nothing records nothing.
    const int steps = session->history.undoCount();
    session->setIsImporting(true);
    QVERIFY(!shown.drop(shown.rows({0}), Qt::MoveAction | Qt::CopyAction, shown.below()));
    session->setIsImporting(false);
    QCOMPARE(session->history.undoCount(), steps);
}

void LayerDragTests::anAltDragDropsDuplicates()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    const int steps = session->history.undoCount();
    QVERIFY(shown.drop(shown.rows({1}), Qt::CopyAction, shown.top(0)));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1 copy", "Layer 2", "Layer 1"}));
    QCOMPARE(shown.lastAction, Qt::CopyAction);
    QCOMPARE(session->history.undoName(), QString("Duplicate Layer"));
    QCOMPARE(session->history.undoCount(), steps + 1);
    QCOMPARE(session->activeLayerID(), std::optional(shown.id(0)));
    QVERIFY(shown.drop(shown.rows({1, 2}), Qt::CopyAction, shown.below()));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1 copy", "Layer 2", "Layer 1", "Layer 2 copy", "Layer 1 copy"}));
    QCOMPARE(session->history.undoName(), QString("Duplicate Layers"));
    session->undo();
    QCOMPARE(shown.names().size(), size_t(3));
    // A folder among the dragged rows copies with its contents.
    const QUuid held = shown.id(2);
    session->selectLayer(held);
    session->addGroup();
    QVERIFY(session->placeLayer(held, session->activeLayerID().value()));
    QCOMPARE(shown.names().size(), size_t(4));
    QVERIFY(session->document().value().layers[size_t(indexOf(session->document().value().layers, shown.id(2)))].isGroup);
    QVERIFY(shown.drop(shown.rows({0, 2}), Qt::CopyAction, shown.below()));
    QCOMPARE(session->history.undoName(), QString("Duplicate Layers"));
    const std::vector<ImageLayer> &layers = session->document().value().layers;
    QCOMPARE(int(layers.size()), 7);
    std::vector<QUuid> folders;
    for (const ImageLayer &layer : layers) {
        if (layer.isGroup)
            folders.push_back(layer.id);
    }
    QCOMPARE(int(folders.size()), 2);
    for (const QUuid &folder : folders)
        QCOMPARE(int(session->descendantIDs(folder).size()), 1);
}

void LayerDragTests::aMaskDragCopiesTheMaskOntoTheRowUnderIt()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    session->selectLayer(shown.id(1));
    session->addLayerMask();
    const auto mask = [&](int row) {
        auto *data = new QMimeData;
        data->setData(NativeLayerList::maskType, uuidString(shown.id(row)).toUtf8());
        return data;
    };
    QVERIFY(!shown.drop(mask(1), Qt::CopyAction, shown.middle(1)));
    QVERIFY(!shown.drop(mask(0), Qt::CopyAction, shown.middle(1)));
    QVERIFY(!layerWith(*session, shown.id(0)).mask.has_value());
    QVERIFY(shown.drop(mask(1), Qt::CopyAction, shown.middle(0)));
    QVERIFY(layerWith(*session, shown.id(0)).mask.has_value());
    QCOMPARE(session->history.undoName(), QString("Copy Layer Mask"));
    QVERIFY(!shown.drop(mask(1), Qt::CopyAction, shown.below()));
    // No-id payloads name no mask, zeros included.
    rewrite(*session, [&](ProjectSnapshot &snapshot) { record(snapshot, shown.id(1)).id = QUuid(); });
    QCOMPARE(shown.id(1), QUuid());
    session->selectLayer(QUuid());
    session->addLayerMask();
    QVERIFY(session->canCopyMask(QUuid(), shown.id(0)));
    const int steps = session->history.undoCount();
    auto *garbage = new QMimeData;
    garbage->setData(NativeLayerList::maskType, "invalid mask id");
    QVERIFY(!shown.drop(garbage, Qt::CopyAction, shown.middle(0)));
    QCOMPARE(session->history.undoCount(), steps);
}

void LayerDragTests::theIndicatorShowsWhereTheDropLands()
{
    const auto session = sessionWithLayers(3);
    Shown shown(*session);
    session->selectLayer(shown.id(0));
    session->addGroup();
    auto *column = shown.list.findChild<LayerColumn *>();
    QVERIFY(column != nullptr);
    const auto enter = [&](QMimeData *data, QPoint listPoint) {
        const QPoint inViewport = shown.list.viewport()->mapFromParent(listPoint);
        QDragEnterEvent event(inViewport, Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(shown.list.viewport(), &event);
        delete data;
        return event.isAccepted();
    };
    // A line above the row; a folder's middle fills it.
    QVERIFY(enter(shown.rows({3}), shown.top(2)));
    QCOMPARE(column->indicator, std::optional(QRect(0, shown.list.cells().at(2)->geometry().top() - 2, column->width(), 2)));
    QVERIFY(!column->indicatorFills);
    QVERIFY(enter(shown.rows({3}), shown.middle(0)));
    QCOMPARE(column->indicator, std::optional(shown.list.cells().at(0)->geometry()));
    QVERIFY(column->indicatorFills);
    QVERIFY(enter(shown.rows({3}), shown.below()));
    QCOMPARE(column->indicator, std::optional(QRect(0, shown.list.cells().back()->geometry().bottom(), column->width(), 2)));
    // Refused, nothing shows; left, nothing stays.
    QVERIFY(!enter(shown.rows({0}), shown.middle(0)));
    QVERIFY(!column->indicator.has_value());
    QVERIFY(enter(shown.rows({3}), shown.top(2)));
    QDragLeaveEvent leave;
    QApplication::sendEvent(shown.list.viewport(), &leave);
    QVERIFY(!column->indicator.has_value());
}

void LayerDragTests::anotherListsRowsLandNowhere()
{
    const auto session = sessionWithLayers(2);
    Shown shown(*session);
    NativeLayerList other(*session);
    // The same ids, dragged from another list: refused.
    std::unique_ptr<QMimeData> foreign = other.dragData(*other.cells().at(1));
    auto *copy = new QMimeData;
    for (const QString &format : foreign->formats())
        copy->setData(format, foreign->data(format));
    QVERIFY(!shown.drop(copy, Qt::MoveAction | Qt::CopyAction, shown.top(0)));
    std::unique_ptr<QMimeData> own = shown.list.dragData(*shown.list.cells().at(1));
    auto *mine = new QMimeData;
    for (const QString &format : own->formats())
        mine->setData(format, own->data(format));
    QVERIFY(shown.drop(mine, Qt::MoveAction | Qt::CopyAction, shown.top(0)));
    QCOMPARE(shown.names(), (std::vector<QString>{"Layer 1", "Layer 2"}));
}

void LayerDragTests::aDragNearTheEdgeScrollsTheList()
{
    const auto session = sessionWithLayers(15);
    Shown shown(*session);
    shown.list.resize(252, 200);
    QTRY_VERIFY(shown.list.verticalScrollBar()->maximum() > 0);
    QMimeData *data = shown.rows({0});
    const QPoint edge(100, shown.list.viewport()->height() - 2);
    QDragEnterEvent enter(edge, Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &enter);
    // Held at the bottom edge, each move scrolls further.
    for (int each = 0; each < 3; ++each) {
        QDragMoveEvent move(edge, Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(shown.list.viewport(), &move);
    }
    QCOMPARE(shown.list.verticalScrollBar()->value(), 42);
    auto *column = shown.list.findChild<LayerColumn *>();
    QVERIFY(column->indicator.has_value());
    QDragMoveEvent top(QPoint(100, 1), Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &top);
    QCOMPARE(shown.list.verticalScrollBar()->value(), 27);
    QDragLeaveEvent leave;
    QApplication::sendEvent(shown.list.viewport(), &leave);
    delete data;
}

void LayerDragTests::aDragHeldAtTheEdgeScrollsOnAndAClipRefreshesTheCursor()
{
    const auto session = sessionWithLayers(15);
    Shown shown(*session);
    shown.list.resize(252, 200);
    QTRY_VERIFY(shown.list.verticalScrollBar()->maximum() > 0);
    QMimeData *data = shown.rows({0});
    const QPoint edge(100, shown.list.viewport()->height() - 2);
    QDragEnterEvent enter(edge, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &enter);
    QDragMoveEvent move(edge, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &move);
    // One move, then a still pointer: the list scrolls on.
    QCOMPARE(shown.list.verticalScrollBar()->value(), 14);
    QVERIFY(shown.list.autoscrolling());
    QTRY_VERIFY(shown.list.verticalScrollBar()->value() >= 70);
    auto *column = shown.list.findChild<LayerColumn *>();
    QVERIFY(column->indicator.has_value());
    // Back inside the scrolling rests; left, it stops.
    QDragMoveEvent inside(QPoint(100, 100), Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &inside);
    QVERIFY(!shown.list.autoscrolling());
    const int resting = shown.list.verticalScrollBar()->value();
    QTest::qWait(150);
    QCOMPARE(shown.list.verticalScrollBar()->value(), resting);
    QDragLeaveEvent leave;
    QApplication::sendEvent(shown.list.viewport(), &leave);
    const int rested = shown.list.verticalScrollBar()->value();
    QTest::qWait(150);
    QCOMPARE(shown.list.verticalScrollBar()->value(), rested);
    QVERIFY(!column->indicator.has_value());
    delete data;
    // An Alt click on the strip clips; the cursor follows.
    shown.list.verticalScrollBar()->setValue(0);
    LayerCell &row = *shown.list.cells().at(0);
    const QPoint strip(row.width() - 20, LayerCell::rowHeight - 4);
    QTest::mouseClick(&row, Qt::LeftButton, Qt::AltModifier, strip);
    QVERIFY(layerWith(*session, shown.id(0)).maskSourceID.has_value());
    QCOMPARE(image(shown.list.viewport()->cursor()), image(NativeLayerList::clippingCursor(true, shown.list.devicePixelRatio())));
    QTest::mouseClick(&row, Qt::LeftButton, Qt::AltModifier, strip);
    QCOMPARE(image(shown.list.viewport()->cursor()), image(NativeLayerList::clippingCursor(false, shown.list.devicePixelRatio())));
}

void LayerDragTests::leavingOrDroppingAtTheEdgeStopsTheScrolling()
{
    const auto session = sessionWithLayers(15);
    Shown shown(*session);
    shown.list.resize(252, 200);
    QTRY_VERIFY(shown.list.verticalScrollBar()->maximum() > 0);
    const QPoint edge(100, shown.list.viewport()->height() - 2);
    const auto scrollAtTheEdge = [&](QMimeData *data) {
        QDragEnterEvent enter(edge, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(shown.list.viewport(), &enter);
        QDragMoveEvent move(edge, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(shown.list.viewport(), &move);
        QVERIFY(shown.list.autoscrolling());
    };
    // Left at the edge, the timer stops for good.
    QMimeData *data = shown.rows({0});
    scrollAtTheEdge(data);
    QDragLeaveEvent leave;
    QApplication::sendEvent(shown.list.viewport(), &leave);
    QVERIFY(!shown.list.autoscrolling());
    delete data;
    const int left = shown.list.verticalScrollBar()->value();
    QTest::qWait(120);
    QCOMPARE(shown.list.verticalScrollBar()->value(), left);
    // Dropped at the edge, the same; the row lands.
    data = shown.rows({0});
    scrollAtTheEdge(data);
    QDropEvent drop(edge, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(shown.list.viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(!shown.list.autoscrolling());
    delete data;
    const int dropped = shown.list.verticalScrollBar()->value();
    QTest::qWait(120);
    QCOMPARE(shown.list.verticalScrollBar()->value(), dropped);
    QCOMPARE(session->history.undoName(), QString("Move Layer"));
}

QTEST_MAIN(LayerDragTests)
#include "LayerDragTests.moc"
