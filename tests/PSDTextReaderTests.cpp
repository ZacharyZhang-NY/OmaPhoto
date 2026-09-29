#include "IO/PSD/PSDText.h"
#include "IO/PSD/PSDText+Descriptor.h"
#include "IO/PSD/PSDText+Engine.h"
#include "PSDFixture+Text.h"
#include <QtTest>

// The type block's two readers: engine dictionaries and descriptors.
namespace {
// A descriptor of these items, read back.
std::optional<PSDDescriptor::Items> described(const std::vector<std::pair<QByteArray, QByteArray>> &items, quint32 version = 16)
{
    PSDFixture::Buffer data;
    PSDFixture::descriptor(data, "Test", items);
    data.data.replace(0, 4, QByteArray("\0\0\0", 3) + char(version));
    PSDDescriptor::Reader reader{data.data};
    return reader.descriptor(true);
}

QByteArray unicode(const QByteArray &type, const QString &text)
{
    return type + PSDFixture::textItem(text).mid(4);
}

std::optional<PSDTextEngine::Engine> engine(const char *text)
{
    return PSDTextEngine::parse(QByteArray(text));
}
}

class PSDTextReaderTests : public QObject {
    Q_OBJECT
private slots:
    void theEngineReadsItsPostScriptForms();
    void deepNestingReadsAsBrokenWithoutACrash();
    void theDescriptorReadsEveryType();
    void theDescriptorReadsNestingAndNames();
    void loneSurrogatesReadAsReplacements();
    void listsAndReferencesStopAtTenThousand();
    void coloursReadInEachForm();
    void theSourcesKeepSwiftsPrecedence();
    void theStyleRoundsAndScalesAsSwift();
    void versionsAndLimitsHoldAtTheirEdges();
};

void PSDTextReaderTests::theEngineReadsItsPostScriptForms()
{
    using namespace PSDTextEngine;
    const Engine read = engine("junk << /A <FEFF00480069> /B (a\\101\\n\\(\\\nz) /C [1 -2.5 .5 +3 1e2 1.] % note\n"
                               " /D true /E null /F /Name /G <41 4 2> /A2 << /K false >> >>")
                            .value();
    QCOMPARE(string(walk(&read, {"A"})).value(), QString("Hi"));
    QCOMPARE(string(walk(&read, {"B"})).value(), QString("aA\n(z"));
    const Array numbers = array(walk(&read, {"C"}));
    QCOMPARE(numbers.size(), size_t(6));
    const std::vector<double> expected{1, -2.5, 0.5, 3, 100, 1};
    for (size_t index = 0; index < expected.size(); ++index)
        QCOMPARE(number(&numbers[index]).value(), expected[index]);
    QCOMPARE(boolean(walk(&read, {"D"})).value(), true);
    QCOMPARE(string(walk(&read, {"E"})).value(), QString());
    QCOMPARE(string(walk(&read, {"F"})).value(), QString("Name"));
    QCOMPARE(string(walk(&read, {"G"})).value(), QString("AB"));
    QCOMPARE(boolean(walk(&read, {"A2", "K"})).value(), false);
    QVERIFY(!walk(&read, {"A", "B"}));
    // Overflow reads as infinity, as Swift's `Double` reads it.
    const Engine huge = engine("<< /X 1e999 >>").value();
    QVERIFY(std::isinf(number(walk(&huge, {"X"})).value()));
    // Broken forms fail the whole dictionary.
    for (const char *broken : {"<< /X - >>", "<< /X 1e >>", "<< /X [1 >>", "<< X 1 >>", "<< /X (a\\", "[1 2]", "<< /X 1", "<< /X <41 >>"})
        QVERIFY2(!engine(broken), broken);
    // Three octal digits; `)` ends names; words stand alone.
    const Engine octal = engine("<< /X (\\1011) >>").value();
    QCOMPARE(string(walk(&octal, {"X"})).value(), QString("A1"));
    QVERIFY(!engine("<< /A [/B)] >>"));
    QVERIFY(!engine("<< /X [true5] >>"));
    // A name with a high byte reads empty.
    const Engine high = engine("<< /\xC3\xA9 1 >>").value();
    QCOMPARE(number(walk(&high, {""})).value(), 1.0);
    // An odd UTF-16 string reads empty.
    const Engine odd = engine("<< /X <FEFF0048 00> >>").value();
    QCOMPARE(string(walk(&odd, {"X"})).value(), QString());
}

void PSDTextReaderTests::deepNestingReadsAsBrokenWithoutACrash()
{
    const QByteArray deep = "<< /X " + QByteArray(200'000, '[') + QByteArray(200'000, ']') + " >>";
    QVERIFY(!PSDTextEngine::parse(deep));
    const QByteArray fits = "<< /X " + QByteArray(500, '[') + QByteArray(500, ']') + " >>";
    QVERIFY(PSDTextEngine::parse(fits));
    // A descriptor of lists within lists, far past the cap.
    PSDFixture::Buffer nested;
    nested.u32(16);
    nested.u32(0);
    PSDFixture::id(nested, "TxLr");
    nested.u32(1);
    PSDFixture::id(nested, "Deep");
    for (int depth = 0; depth < 100'000; ++depth) {
        nested.string("VlLs");
        nested.u32(1);
    }
    nested.string("long");
    nested.i32(1);
    PSDDescriptor::Reader reader{nested.data};
    QVERIFY(!reader.descriptor(true));
}

void PSDTextReaderTests::theDescriptorReadsEveryType()
{
    using PSDFixture::Buffer;
    Buffer comp, flag, alias, reference, global, type;
    comp.string("comp");
    comp.u64(quint64(-2));
    flag.string("bool");
    flag.u8(1);
    alias.string("alis");
    alias.u32(3);
    alias.string("abc");
    reference.string("obj ");
    reference.u32(7);
    for (const char *form : {"prop", "Clss", "Enmr", "rele", "Idnt", "indx", "name"}) {
        reference.string(form);
        const QByteArray name = PSDFixture::textItem(QStringLiteral("n")).mid(4);
        const std::map<QByteArray, int> identifiers{{"prop", 2}, {"Clss", 1}, {"Enmr", 3}, {"rele", 1}};
        if (QByteArray(form) != "Idnt" && QByteArray(form) != "indx")
            reference.bytes(name);
        for (int count = 0; count < (identifiers.contains(form) ? identifiers.at(form) : 0); ++count)
            PSDFixture::id(reference, "Idnt");
        if (QByteArray(form) == "rele" || QByteArray(form) == "Idnt" || QByteArray(form) == "indx")
            reference.i32(4);
    }
    global.bytes(unicode("GlbC", QStringLiteral("g")));
    PSDFixture::id(global, "Clss");
    type.bytes(unicode("type", QStringLiteral("t")));
    PSDFixture::id(type, "Clss");
    Buffer box;
    box.string("Objc");
    box.u32(0);
    PSDFixture::id(box, "Rctn");
    box.u32(4);
    for (const auto &[key, value] : std::vector<std::pair<QByteArray, double>>{{"Left", 1}, {"Top", 2}, {"Rght", 5}, {"Btom", 9}}) {
        PSDFixture::id(box, key);
        box.string("doub");
        PSDFixture::f64(box, value);
    }
    const PSDDescriptor::Items items = described({{"C", comp.data}, {"B", flag.data}, {"A", alias.data}, {"R", reference.data}, {"G", global.data},
                                                  {"T", type.data}, {"box", box.data}, {"Txt ", PSDFixture::textItem(QStringLiteral("after"))}})
                                           .value();
    QCOMPARE(std::get<double>(items.at("C").value), -2.0);
    QCOMPARE(std::get<double>(items.at("B").value), 0.0);
    QCOMPARE(PSDDescriptor::string(items, QStringLiteral("Txt ")).value(), QString("after"));
    // `Top` without its space still reads, as Swift trims.
    QCOMPARE(PSDDescriptor::rect(items, QStringLiteral("box")).value(), QRectF(1, 2, 4, 7));
    // An infinite side is no rectangle.
    Buffer infinite = box;
    infinite.data.chop(8);
    PSDFixture::f64(infinite, std::numeric_limits<double>::infinity());
    QVERIFY(!PSDDescriptor::rect(described({{"box", infinite.data}}).value(), QStringLiteral("box")));
    // Refusals: a version, a high byte, and each cap.
    QVERIFY(!described({{"Txt ", PSDFixture::textItem(QStringLiteral("x"))}}, 15));
    QVERIFY(!described({{"K\x80", PSDFixture::textItem(QStringLiteral("x"))}}));
    Buffer data;
    data.string("tdta");
    data.u32(8'000'001);
    data.bytes(QByteArray(8'000'001, 'x'));
    QVERIFY(!described({{"D", data.data}}));
    Buffer longAlias;
    longAlias.string("alis");
    longAlias.u32(8'000'001);
    longAlias.bytes(QByteArray(8'000'001, 'x'));
    QVERIFY(!described({{"A", longAlias.data}}));
    Buffer words;
    words.string("TEXT");
    words.u32(1'000'001);
    words.bytes(QByteArray(2'000'002, 'x'));
    QVERIFY(!described({{"W", words.data}}));
    std::vector<std::pair<QByteArray, QByteArray>> many;
    for (int index = 0; index <= 10'000; ++index)
        many.emplace_back(QByteArray::number(index), QByteArray("long\0\0\0\1", 8));
    QVERIFY(!described(many));
    many.pop_back();
    QCOMPARE(described(many).value().size(), size_t(10'000));
}

void PSDTextReaderTests::theDescriptorReadsNestingAndNames()
{
    using PSDFixture::Buffer;
    Buffer global, list;
    global.string("GlbO");
    global.u32(0);
    PSDFixture::id(global, "Innr");
    global.u32(1);
    PSDFixture::id(global, "N");
    global.bytes(QByteArray("long\0\0\0\7", 8));
    list.string("VlLs");
    list.u32(2);
    list.bytes(QByteArray("long\0\0\0\1", 8));
    list.bytes(QByteArray("long\0\0\0\2", 8));
    const PSDDescriptor::Items items = described({{"G", global.data}, {"L", list.data}}).value();
    QCOMPARE(std::get<double>(std::get<PSDDescriptor::Items>(items.at("G").value).at("N").value), 7.0);
    const auto &values = std::get<std::vector<PSDDescriptor::Value>>(items.at("L").value);
    QCOMPARE(values.size(), size_t(2));
    QCOMPARE(std::get<double>(values[1].value), 2.0);
    // A key of 10,000 bytes reads; one more refuses.
    QVERIFY(described({{QByteArray(10'000, 'k'), QByteArray("long\0\0\0\1", 8)}}));
    QVERIFY(!described({{QByteArray(10'001, 'k'), QByteArray("long\0\0\0\1", 8)}}));
    // The exact side's key decides, number or not.
    Buffer box;
    box.string("Objc");
    box.u32(0);
    PSDFixture::id(box, "Rctn");
    box.u32(5);
    for (const auto &[key, value] : std::vector<std::pair<QByteArray, double>>{{"Left", 0}, {"Top", 0}, {"Rght", 200}, {"Btom", 80}}) {
        PSDFixture::id(box, key);
        box.string("doub");
        PSDFixture::f64(box, value);
    }
    PSDFixture::id(box, "Top ");
    box.bytes(PSDFixture::textItem(QStringLiteral("bad")));
    QVERIFY(!PSDDescriptor::rect(described({{"box", box.data}}).value(), QStringLiteral("box")));
    // Lowercase hex reads as uppercase.
    const PSDTextEngine::Engine lower = engine("<< /X <feff006a> >>").value();
    QCOMPARE(PSDTextEngine::string(PSDTextEngine::walk(&lower, {"X"})).value(), QString("j"));
}

void PSDTextReaderTests::loneSurrogatesReadAsReplacements()
{
    const QString replaced = QString(QChar::ReplacementCharacter);
    const std::vector<std::pair<QByteArray, QString>> cases{{"0048D800", "H" + replaced},
                                                            {"D800", replaced},
                                                            {"DC00", replaced},
                                                            {"0048D8000049", "H" + replaced + "I"},
                                                            {"D800DC00", QString::fromUtf16(u"\xD800\xDC00")}};
    for (const auto &[units, expected] : cases) {
        const PSDTextEngine::Engine read = engine(("<< /X <FEFF" + units + "> >>").constData()).value();
        QCOMPARE(PSDTextEngine::string(PSDTextEngine::walk(&read, {"X"})).value(), expected);
        PSDFixture::Buffer text;
        text.string("TEXT");
        text.u32(quint32(units.size() / 4));
        text.bytes(QByteArray::fromHex(units));
        QCOMPARE(PSDDescriptor::string(described({{"Txt ", text.data}}).value(), QStringLiteral("Txt ")).value(), expected);
    }
}

void PSDTextReaderTests::listsAndReferencesStopAtTenThousand()
{
    for (const quint32 count : {10'000u, 10'001u}) {
        PSDFixture::Buffer list, reference;
        list.string("VlLs");
        list.u32(count);
        reference.string("obj ");
        reference.u32(count);
        for (quint32 index = 0; index < count; ++index) {
            list.bytes(QByteArray("long\0\0\0\1", 8));
            reference.string("Idnt");
            reference.i32(1);
        }
        QCOMPARE(described({{"L", list.data}}).has_value(), count == 10'000u);
        QCOMPARE(described({{"R", reference.data}}).has_value(), count == 10'000u);
    }
}

void PSDTextReaderTests::coloursReadInEachForm()
{
    using PSDFixture::TypeBlock;
    const auto colour = [](const QByteArray &values) {
        TypeBlock block;
        block.text = QStringLiteral("Hello");
        block.values = values;
        const LayerTextStyle style = PSDText::parse(PSDFixture::typeExtra(block)).value().style;
        return std::array{style.red, style.green, style.blue};
    };
    QCOMPARE(colour("1.0 0 0 1"), (std::array{0.0, 0.0, 1.0}));
    QCOMPARE(colour("0.2 0.4 0.6"), (std::array{0.2, 0.4, 0.6}));
    QCOMPARE(colour("0.6"), (std::array{0.6, 0.6, 0.6}));
    // A red or green difference alone is a second style.
    for (const char *values : {"[ 1.0 1 0 0 ]", "[ 1.0 0 1 0 ]"}) {
        TypeBlock block;
        block.text = QStringLiteral("Hello");
        block.secondOverride = QByteArray("/FillColor << /Values ") + values + " >>\n";
        QCOMPARE(PSDText::parse(PSDFixture::typeExtra(block)).value().notes, std::vector{PSDText::firstStyleNote});
    }
}

void PSDTextReaderTests::theSourcesKeepSwiftsPrecedence()
{
    using PSDFixture::TypeBlock;
    // Present but empty words refuse; absent ones read the engine.
    TypeBlock empty;
    empty.engineText = QStringLiteral("Fallback");
    QVERIFY(!PSDText::parse(PSDFixture::typeExtra(empty)));
    empty.typed = false;
    QCOMPARE(PSDText::parse(PSDFixture::typeExtra(empty)).value().style.content, QString("Fallback"));
    // `TySh` wins over `tySh`, broken or not.
    TypeBlock upper, lower;
    upper.text = QStringLiteral("Upper");
    lower.text = QStringLiteral("Lower");
    QVERIFY(!PSDText::parse({{QStringLiteral("TySh"), QByteArray("\0\1", 2)}, {QStringLiteral("tySh"), PSDFixture::tySh(lower)}}));
    const PSDText::Source both = PSDText::parse({{QStringLiteral("TySh"), PSDFixture::tySh(upper)}, {QStringLiteral("tySh"), PSDFixture::tySh(lower)}}).value();
    QCOMPARE(both.style.content, QString("Upper"));
}

void PSDTextReaderTests::theStyleRoundsAndScalesAsSwift()
{
    using PSDFixture::TypeBlock;
    const auto parsed = [](const TypeBlock &block) { return PSDText::parse(PSDFixture::typeExtra(block)).value(); };
    // Manual leading scales with the matrix, as the size does.
    TypeBlock scaled;
    scaled.text = QStringLiteral("H\nH");
    scaled.leading = 30;
    scaled.xx = scaled.yy = 2;
    QCOMPARE(parsed(scaled).style.fontSize, 48.0);
    QCOMPARE(parsed(scaled).style.leading, 60.0);
    // The font index and justification round half away from zero.
    TypeBlock faces;
    faces.text = QStringLiteral("Hello");
    faces.font = QStringLiteral("DejaVuSans");
    faces.secondFont = QStringLiteral("DejaVuSerif");
    const std::vector<std::pair<QByteArray, QString>> indices{{"1", "DejaVuSerif"}, {"0.6", "DejaVuSerif"}, {"0.4", "DejaVuSans"}, {"-0.6", "Helvetica"}};
    for (const auto &[index, name] : indices) {
        faces.fontIndex = index;
        QCOMPARE(parsed(faces).style.fontName, name);
    }
    faces.justification = "1.6";
    QVERIFY(parsed(faces).style.alignment == TextAlignment::center);
    QVERIFY(parsed(faces).notes.empty());
    faces.justification = "2.5";
    QVERIFY(parsed(faces).style.alignment == TextAlignment::left);
    QCOMPARE(parsed(faces).notes, std::vector{PSDText::justifyNote});
    faces.justification = "0.4";
    QVERIFY(parsed(faces).notes.empty());
    // `Txt` without its space holds the words over the engine's.
    TypeBlock alias;
    alias.text = QStringLiteral("Alias words");
    alias.wordsKey = "Txt";
    alias.engineText = QStringLiteral("Fallback");
    QCOMPARE(parsed(alias).style.content, QString("Alias words"));
    alias.withEngine = false;
    QCOMPARE(parsed(alias).style.content, QString("Alias words"));
    // A negative exponent keeps the engine and its style.
    TypeBlock tracked;
    tracked.text = QStringLiteral("Hello");
    tracked.tracking = "1e-1";
    tracked.red = 1;
    QCOMPARE(parsed(tracked).style.tracking, 0.0024);
    QCOMPARE(parsed(tracked).style.red, 1.0);
    const PSDTextEngine::Engine small = engine("<< /X -2.5E-2 >>").value();
    QCOMPARE(PSDTextEngine::number(PSDTextEngine::walk(&small, {"X"})).value(), -0.025);
}

void PSDTextReaderTests::versionsAndLimitsHoldAtTheirEdges()
{
    using PSDFixture::TypeBlock;
    TypeBlock block;
    block.text = QStringLiteral("Hello");
    const QByteArray whole = PSDFixture::tySh(block);
    QVERIFY(PSDText::parse({{QStringLiteral("TySh"), whole}}));
    // Object version 1 and text version 50, whole blocks otherwise.
    QByteArray object = whole, text = whole;
    object[1] = 2;
    text[51] = 51;
    QVERIFY(!PSDText::parse({{QStringLiteral("TySh"), object}}));
    QVERIFY(!PSDText::parse({{QStringLiteral("TySh"), text}}));
    // 100,000 UTF-16 units at most.
    block.text = QString(100'000, u'x');
    QCOMPARE(PSDText::parse(PSDFixture::typeExtra(block)).value().style.content.size(), qsizetype(100'000));
    block.text += u'x';
    QVERIFY(!PSDText::parse(PSDFixture::typeExtra(block)));
    // 511 lists round a number read; 512 refuse.
    for (const int depth : {511, 512}) {
        PSDFixture::Buffer lists;
        for (int level = 0; level < depth; ++level) {
            lists.string("VlLs");
            lists.u32(1);
        }
        lists.bytes(QByteArray("long\0\0\0\1", 8));
        QCOMPARE(described({{"Deep", lists.data}}).has_value(), depth == 511);
    }
}

QTEST_GUILESS_MAIN(PSDTextReaderTests)
#include "PSDTextReaderTests.moc"
