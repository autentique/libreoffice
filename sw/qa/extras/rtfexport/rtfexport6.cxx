/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <swmodeltestbase.hxx>

#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/graphic/GraphicType.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <com/sun/star/style/TabStop.hpp>
#include <com/sun/star/text/TableColumnSeparator.hpp>
#include <com/sun/star/text/XFootnotesSupplier.hpp>
#include <com/sun/star/text/XPageCursor.hpp>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>
#include <com/sun/star/text/XTextTablesSupplier.hpp>
#include <com/sun/star/text/XTextFramesSupplier.hpp>
#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/text/WritingMode2.hpp>
#include <com/sun/star/text/XTextContentAppend.hpp>
#include <com/sun/star/text/XTextDocument.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/FontUnderline.hpp>

#include <tools/UnitConversion.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <comphelper/scopeguard.hxx>

using namespace css;

class Test : public SwModelTestBase
{
public:
    Test()
        : SwModelTestBase("/sw/qa/extras/rtfexport/data/", "Rich Text Format")
    {
    }
};

DECLARE_RTFEXPORT_TEST(testFdo85889pc, "fdo85889-pc.rtf")
{
    uno::Reference<text::XTextRange> xTextRange = getRun(getParagraph(1), 1);

    CPPUNIT_ASSERT_EQUAL(OUString(u"\u00B1\u2265\u2264"), xTextRange->getString());
}

DECLARE_RTFEXPORT_TEST(testFdo85889pca, "fdo85889-pca.rtf")
{
    uno::Reference<text::XTextRange> xTextRange = getRun(getParagraph(1), 1);

    CPPUNIT_ASSERT_EQUAL(OUString(u"\u00B1\u2017\u00BE"), xTextRange->getString());
}

DECLARE_RTFEXPORT_TEST(testFdo85889mac, "fdo85889-mac.rtf")
{
    uno::Reference<text::XTextRange> xTextRange = getRun(getParagraph(1), 1);

    CPPUNIT_ASSERT_EQUAL(OUString(u"\u00D2\u00DA\u00DB"), xTextRange->getString());
}

CPPUNIT_TEST_FIXTURE(Test, testFdo72031)
{
    auto verify = [this]() {
        CPPUNIT_ASSERT_EQUAL(OUString(u"\uF0C5"), getRun(getParagraph(1), 1)->getString());
    };

    AllSettings aSavedSettings = Application::GetSettings();
    AllSettings aSettings(aSavedSettings);
    aSettings.SetLanguageTag(LanguageTag("ru"));
    Application::SetSettings(aSettings);
    comphelper::ScopeGuard g([&aSavedSettings] { Application::SetSettings(aSavedSettings); });

    createSwDoc("fdo72031.rtf");
    verify();
    saveAndReload("Rich Text Format");
    verify();
}

DECLARE_RTFEXPORT_TEST(testFdo86750, "fdo86750.rtf")
{
    // This was 'HYPERLINK#anchor', the URL of the hyperlink had the field type as a prefix, leading to broken links.
    CPPUNIT_ASSERT_EQUAL(OUString("#anchor"),
                         getProperty<OUString>(getRun(getParagraph(1), 1), "HyperLinkURL"));
}

DECLARE_RTFEXPORT_TEST(testTdf88811, "tdf88811.rtf")
{
    // The problem was that shapes anchored to the paragraph that is moved into a textframe were lost, so this was 2.
    CPPUNIT_ASSERT_EQUAL(4, getShapes());
}

DECLARE_RTFEXPORT_TEST(testFdo49893_2, "fdo49893-2.rtf")
{
    // Ensure that header text exists on each page (especially on second page)
    CPPUNIT_ASSERT_EQUAL(OUString("HEADER"), parseDump("/root/page[1]/header/txt/text()"));
    CPPUNIT_ASSERT_EQUAL(OUString("HEADER"), parseDump("/root/page[2]/header/txt/text()"));
    CPPUNIT_ASSERT_EQUAL(OUString("HEADER"), parseDump("/root/page[3]/header/txt/text()"));
}

DECLARE_RTFEXPORT_TEST(testFdo89496, "fdo89496.rtf")
{
    // Just ensure that document is loaded and shape exists
    uno::Reference<drawing::XShape> xShape = getShape(1);
    CPPUNIT_ASSERT(xShape.is());
}

DECLARE_RTFEXPORT_TEST(testFdo75614, "tdf75614.rtf")
{
    // Text after the footnote was missing, so this resulted in a css::container::NoSuchElementException.
    CPPUNIT_ASSERT_EQUAL(OUString("after."), getRun(getParagraph(1), 3)->getString());
}

DECLARE_RTFEXPORT_TEST(mathtype, "mathtype.rtf")
{
    OUString aFormula = getFormula(getRun(getParagraph(1), 1));
    CPPUNIT_ASSERT(!aFormula.isEmpty());
}

DECLARE_RTFEXPORT_TEST(testTdf86182, "tdf86182.rtf")
{
    // Writing mode was the default, i.e. text::WritingMode2::CONTEXT.
    CPPUNIT_ASSERT_EQUAL(text::WritingMode2::RL_TB,
                         getProperty<sal_Int16>(getParagraph(1), "WritingMode"));
}

DECLARE_RTFEXPORT_TEST(testTdf91074, "tdf91074.rtf")
{
    // The file failed to load, as the border color was imported using the LineColor UNO property.
    uno::Reference<drawing::XShape> xShape = getShape(1);
    CPPUNIT_ASSERT_EQUAL(
        COL_LIGHTRED,
        Color(ColorTransparency, getProperty<table::BorderLine2>(xShape, "TopBorder").Color));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf90260Nopar)
{
    createSwDoc("hello.rtf");
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xText = xTextDocument->getText();
    uno::Reference<text::XTextRange> xEnd = xText->getEnd();
    paste(u"rtfexport/data/tdf90260-nopar.rtf", "com.sun.star.comp.Writer.RtfFilter", xEnd);
    CPPUNIT_ASSERT_EQUAL(1, getParagraphs());
}

DECLARE_RTFEXPORT_TEST(testTdf86814, "tdf86814.rtf")
{
    // This was awt::FontWeight::NORMAL, i.e. the first run wasn't bold, when it should be bold (applied paragraph style with direct formatting).
    CPPUNIT_ASSERT_EQUAL(awt::FontWeight::BOLD,
                         getProperty<float>(getRun(getParagraph(1), 1), "CharWeight"));
}

/** Make sure that the document variable "Unused", which is not referenced in the document,
    is imported and exported. */
DECLARE_RTFEXPORT_TEST(testTdf150267, "tdf150267.rtf")
{
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextFieldsSupplier> xSupplier(xModel, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTextFieldMasters = xSupplier->getTextFieldMasters();
    CPPUNIT_ASSERT_EQUAL(sal_True,
                         xTextFieldMasters->hasByName("com.sun.star.text.fieldmaster.User.Unused"));

    auto xFieldMaster = xTextFieldMasters->getByName("com.sun.star.text.fieldmaster.User.Unused");
    CPPUNIT_ASSERT_EQUAL(OUString("Hello World"), getProperty<OUString>(xFieldMaster, "Content"));
}

DECLARE_RTFEXPORT_TEST(testTdf151370, "tdf151370.rtf")
{
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextFieldsSupplier> xSupplier(xModel, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTextFieldMasters = xSupplier->getTextFieldMasters();
    // Here we try to read/write docvar having non-ascii name and value. So it is encoded in Unicode
    OUString sFieldName(u"com.sun.star.text.fieldmaster.User."
                        "LocalChars\u00c1\u0072\u0076\u00ed\u007a\u0074\u0075\u0072\u006f\u0054"
                        "\u00fc\u006b\u00f6\u0072\u0066\u00fa\u0072\u00f3\u0067\u00e9\u0070");
    CPPUNIT_ASSERT_EQUAL(sal_True, xTextFieldMasters->hasByName(sFieldName));

    auto xFieldMaster = xTextFieldMasters->getByName(sFieldName);
    CPPUNIT_ASSERT_EQUAL(
        OUString(u"\u00e1\u0072\u0076\u00ed\u007a\u0074\u0075\u0072\u006f\u0074\u00fc"
                 "\u006b\u00f6\u0072\u0066\u00fa\u0072\u00f3\u0067\u00e9\u0070"),
        getProperty<OUString>(xFieldMaster, "Content"));
}

DECLARE_RTFEXPORT_TEST(testTdf108416, "tdf108416.rtf")
{
    uno::Reference<container::XNameAccess> xCharacterStyles(getStyles("CharacterStyles"));
    uno::Reference<beans::XPropertySet> xListLabel(xCharacterStyles->getByName("ListLabel 1"),
                                                   uno::UNO_QUERY);
    // This was awt::FontWeight::BOLD, list numbering got an unexpected bold formatting.
    CPPUNIT_ASSERT_EQUAL(awt::FontWeight::NORMAL, getProperty<float>(xListLabel, "CharWeight"));
}

DECLARE_RTFEXPORT_TEST(testBinSkipping, "bin-skipping.rtf")
{
    // before, it was importing '/nMUST NOT IMPORT'
    CPPUNIT_ASSERT_EQUAL(OUString("text"), getRun(getParagraph(1), 1)->getString());
}

DECLARE_RTFEXPORT_TEST(testTdf92061, "tdf92061.rtf")
{
    // This was "C", i.e. part of the footnote ended up in the body text.
    CPPUNIT_ASSERT_EQUAL(OUString("body-after"), getRun(getParagraph(1), 3)->getString());
}

DECLARE_RTFEXPORT_TEST(testTdf92481, "tdf92481.rtf")
{
    // This was 0, RTF_WIDOWCTRL was not imported.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int8>(2),
                         getProperty<sal_Int8>(getParagraph(1), "ParaWidows"));
}

DECLARE_RTFEXPORT_TEST(testTdf94456, "tdf94456.rtf")
{
    // Paragraph left margin and first line indent wasn't imported correctly.

    // This was 1270.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(762),
                         getProperty<sal_Int32>(getParagraph(1), "ParaLeftMargin"));
    // This was -635.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-762),
                         getProperty<sal_Int32>(getParagraph(1), "ParaFirstLineIndent"));
}

DECLARE_RTFEXPORT_TEST(testTdf94435, "tdf94435.rtf")
{
    // This was style::ParagraphAdjust_LEFT, \ltrpar undone the effect of \qc.
    CPPUNIT_ASSERT_EQUAL(
        style::ParagraphAdjust_CENTER,
        static_cast<style::ParagraphAdjust>(getProperty<sal_Int16>(getParagraph(1), "ParaAdjust")));
}

DECLARE_RTFEXPORT_TEST(testTdf54584, "tdf54584.rtf")
{
    uno::Reference<text::XTextFieldsSupplier> xTextFieldsSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XEnumerationAccess> xFieldsAccess(
        xTextFieldsSupplier->getTextFields());
    uno::Reference<container::XEnumeration> xFields(xFieldsAccess->createEnumeration());
    // \PAGE was ignored, so no fields were in document -> exception was thrown
    CPPUNIT_ASSERT_NO_THROW_MESSAGE(
        "No fields in document found: field \"\\PAGE\" was not properly read",
        xFields->nextElement());
}

DECLARE_RTFEXPORT_TEST(testTdf96308Deftab, "tdf96308-deftab.rtf")
{
    uno::Reference<lang::XMultiServiceFactory> xTextFactory(mxComponent, uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xDefaults(
        xTextFactory->createInstance("com.sun.star.text.Defaults"), uno::UNO_QUERY);
    // This was 1270 as \deftab was ignored on import.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(convertTwipToMm100(284)),
                         getProperty<sal_Int32>(xDefaults, "TabStopDistance"));
}

DECLARE_RTFEXPORT_TEST(testLandscape, "landscape.rtf")
{
    // Check landscape flag.
    CPPUNIT_ASSERT_EQUAL(3, getPages());

    // All pages should have flag orientation
    uno::Reference<container::XNameAccess> pageStyles = getStyles("PageStyles");

    // get a page cursor
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextViewCursorSupplier> xTextViewCursorSupplier(
        xModel->getCurrentController(), uno::UNO_QUERY);
    uno::Reference<text::XPageCursor> xCursor(xTextViewCursorSupplier->getViewCursor(),
                                              uno::UNO_QUERY);

    // check that the first page has landscape flag
    xCursor->jumpToFirstPage();
    OUString pageStyleName = getProperty<OUString>(xCursor, "PageStyleName");
    uno::Reference<style::XStyle> xStylePage(pageStyles->getByName(pageStyleName), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(xStylePage, "IsLandscape"));

    // check that the second page has landscape flag
    xCursor->jumpToPage(2);
    pageStyleName = getProperty<OUString>(xCursor, "PageStyleName");
    xStylePage.set(pageStyles->getByName(pageStyleName), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(xStylePage, "IsLandscape"));

    // check that the last page has landscape flag
    xCursor->jumpToLastPage();
    pageStyleName = getProperty<OUString>(xCursor, "PageStyleName");
    xStylePage.set(pageStyles->getByName(pageStyleName), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(xStylePage, "IsLandscape"));
}

DECLARE_RTFEXPORT_TEST(testTdf97035, "tdf97035.rtf")
{
    uno::Reference<text::XTextTablesSupplier> xTextTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTextTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTable(xTables->getByIndex(0), uno::UNO_QUERY);

    // First cell width of the second row should be 2300
    uno::Reference<table::XTableRows> xTableRows = xTable->getRows();
    CPPUNIT_ASSERT_EQUAL(sal_Int16(2300), getProperty<uno::Sequence<text::TableColumnSeparator>>(
                                              xTableRows->getByIndex(1), "TableColumnSeparators")[0]
                                              .Position);
}

DECLARE_RTFEXPORT_TEST(testTdf87034, "tdf87034.rtf")
{
    // This was A1BC34D, i.e. the first "super" text portion was mis-imported,
    // and was inserted instead right before the second "super" text portion.
    CPPUNIT_ASSERT_EQUAL(OUString("A1B3C4D"), getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(Test, testClassificatonPasteLevels)
{
    createSwDoc("classification-confidential.rtf");
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xText = xTextDocument->getText();
    uno::Reference<text::XTextRange> xEnd = xText->getEnd();

    // Classified source and classified destination, but internal only has a
    // higher level than confidential: nothing should happen.
    OUString aOld = xText->getString();
    paste(u"rtfexport/data/classification-yes.rtf", "com.sun.star.comp.Writer.RtfFilter", xEnd);
    CPPUNIT_ASSERT_EQUAL(aOld, xText->getString());
}

DECLARE_RTFEXPORT_TEST(testTdf95707, "tdf95707.rtf")
{
    // Graphic was replaced with a "Read-Error" placeholder.
    uno::Reference<graphic::XGraphic> xGraphic
        = getProperty<uno::Reference<graphic::XGraphic>>(getShape(1), "Graphic");
    CPPUNIT_ASSERT(xGraphic.is());
    CPPUNIT_ASSERT(xGraphic->getType() != graphic::GraphicType::EMPTY);
}

DECLARE_RTFEXPORT_TEST(testTdf96275, "tdf96275.rtf")
{
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(1), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xParagraph = getParagraphOfText(3, xCell->getText());
    // This was text: the shape's frame was part of the 1st paragraph instead of the 3rd one.
    CPPUNIT_ASSERT_EQUAL(OUString("Frame"),
                         getProperty<OUString>(getRun(xParagraph, 1), "TextPortionType"));
}

DECLARE_RTFEXPORT_TEST(testTdf82073, "tdf82073.rtf")
{
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(2), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY);
    // This was -1: the background color was automatic, not black.
    CPPUNIT_ASSERT_EQUAL(COL_BLACK, getProperty<Color>(xCell, "BackColor"));
}

DECLARE_RTFEXPORT_TEST(testTdf74795, "tdf74795.rtf")
{
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(1), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY);
    // This was 0, \trpaddl was ignored on import.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(635),
                         getProperty<sal_Int32>(xCell, "LeftBorderDistance"));

    xCell.set(xTable->getCellByName("A2"), uno::UNO_QUERY);
    // Make sure that the scope of the default is only one row.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0),
                         getProperty<sal_Int32>(xCell, "LeftBorderDistance"));
}

DECLARE_RTFEXPORT_TEST(testTdf137085, "tdf137085.rtf")
{
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(1), uno::UNO_QUERY);
    // \trpaddl0 overrides \trgaph600 (-1058 mm100) and built-in default of 190
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), getProperty<sal_Int32>(xTable, "LeftMargin"));

    // the \trpaddl0 is applied to all cells
    uno::Reference<text::XTextRange> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0),
                         getProperty<sal_Int32>(xCell, "LeftBorderDistance"));

    xCell.set(xTable->getCellByName("B1"), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0),
                         getProperty<sal_Int32>(xCell, "LeftBorderDistance"));
}

DECLARE_RTFEXPORT_TEST(testTdf77349, "tdf77349.rtf")
{
    uno::Reference<container::XNamed> xImage(getShape(1), uno::UNO_QUERY);
    // This was empty: imported image wasn't named automatically.
    CPPUNIT_ASSERT_EQUAL(OUString("Image1"), xImage->getName());
}

DECLARE_RTFEXPORT_TEST(testTdf50821, "tdf50821.rtf")
{
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(2), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY);
    // This was 0, \trpaddfl was mishandled on import.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(191),
                         getProperty<sal_Int32>(xCell, "LeftBorderDistance"));
}

DECLARE_RTFEXPORT_TEST(testTdf100507, "tdf100507.rtf")
{
    // This was 0: left margin of the first paragraph was lost on import.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(6618),
                         getProperty<sal_Int32>(getParagraph(1), "ParaLeftMargin"));
}

DECLARE_RTFEXPORT_TEST(testTdf44986, "tdf44986.rtf")
{
    // Check that the table at the second paragraph.
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(2), uno::UNO_QUERY);
    uno::Reference<table::XTableRows> xTableRows = xTable->getRows();
    // Check the first row of the table, it should have two cells (one separator).
    // This was 0: the first row had no separators, so it had only one cell, which was too wide.
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), getProperty<uno::Sequence<text::TableColumnSeparator>>(
                                           xTableRows->getByIndex(0), "TableColumnSeparators")
                                           .getLength());
}

DECLARE_RTFEXPORT_TEST(testTdf90697, "tdf90697.rtf")
{
    // We want section breaks to be seen as section breaks, not as page breaks,
    // so this document should have only one page, not three.
    CPPUNIT_ASSERT_EQUAL(1, getPages());
}

DECLARE_RTFEXPORT_TEST(testTdf104317, "tdf104317.rtf")
{
    // This failed to load, we tried to set CustomShapeGeometry on a line shape.
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
}

DECLARE_RTFEXPORT_TEST(testTdf104744, "tdf104744.rtf")
{
    auto xRules = getProperty<uno::Reference<container::XIndexAccess>>(
        getStyles("NumberingStyles")->getByName("WWNum1"), "NumberingRules");
    comphelper::SequenceAsHashMap aRule(xRules->getByIndex(0));
    // This was 0.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1270), aRule["IndentAt"].get<sal_Int32>());
}

CPPUNIT_TEST_FIXTURE(SwModelTestBase, testChicagoNumberingFootnote)
{
    // Create a document, set footnote numbering type to SYMBOL_CHICAGO.
    createSwDoc();
    uno::Reference<text::XFootnotesSupplier> xFootnotesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xFootnoteSettings
        = xFootnotesSupplier->getFootnoteSettings();
    sal_uInt16 nNumberingType = style::NumberingType::SYMBOL_CHICAGO;
    xFootnoteSettings->setPropertyValue("NumberingType", uno::Any(nNumberingType));

    // Insert a footnote.
    uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextContent> xFootnote(
        xFactory->createInstance("com.sun.star.text.Footnote"), uno::UNO_QUERY);
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextContentAppend> xTextContentAppend(xTextDocument->getText(),
                                                                uno::UNO_QUERY);
    xTextContentAppend->appendTextContent(xFootnote, {});

    saveAndReload("Rich Text Format");
    xFootnotesSupplier.set(mxComponent, uno::UNO_QUERY);
    sal_uInt16 nExpected = style::NumberingType::SYMBOL_CHICAGO;
    auto nActual
        = getProperty<sal_uInt16>(xFootnotesSupplier->getFootnoteSettings(), "NumberingType");
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 63
    // - Actual  : 4
    // i.e. the numbering type was ARABIC, not SYMBOL_CHICAGO.
    CPPUNIT_ASSERT_EQUAL(nExpected, nActual);
}

DECLARE_RTFEXPORT_TEST(testTdf105852, "tdf105852.rtf")
{
    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTextTable(xTables->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<table::XTableRows> xTableRows = xTextTable->getRows();
    // All rows but last were merged -> there were only 2 rows
    CPPUNIT_ASSERT_EQUAL(sal_Int32(6), xTableRows->getCount());
    // The first row must have 4 cells.
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), getProperty<uno::Sequence<text::TableColumnSeparator>>(
                                           xTableRows->getByIndex(0), "TableColumnSeparators")
                                           .getLength());
    // The third row must have 1 merged cell.
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), getProperty<uno::Sequence<text::TableColumnSeparator>>(
                                           xTableRows->getByIndex(2), "TableColumnSeparators")
                                           .getLength());
}

DECLARE_RTFEXPORT_TEST(testTdf104287, "tdf104287.rtf")
{
    uno::Reference<text::XTextContent> xShape(getShape(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT(xShape.is());
    // This failed, the bitmap had no valid anchor.
    CPPUNIT_ASSERT(xShape->getAnchor().is());
}

DECLARE_RTFEXPORT_TEST(testTdf105729, "tdf105729.rtf")
{
    // This was style::ParagraphAdjust_LEFT, \ltrpar undone the effect of \qc from style.
    CPPUNIT_ASSERT_EQUAL(
        style::ParagraphAdjust_CENTER,
        static_cast<style::ParagraphAdjust>(getProperty<sal_Int16>(getParagraph(1), "ParaAdjust")));
}

DECLARE_RTFEXPORT_TEST(testTdf106694, "tdf106694.rtf")
{
    auto aTabs = getProperty<uno::Sequence<style::TabStop>>(getParagraph(1), "ParaTabStops");
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aTabs.getLength());
    // This was 0, tab position was incorrect, looked like it was missing.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(14605), aTabs[0].Position);
}

DECLARE_RTFEXPORT_TEST(testTdf107116, "tdf107116.rtf")
{
    // This was 0, upper border around text (and its distance) was missing.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(convertTwipToMm100(120)),
                         getProperty<sal_Int32>(getParagraph(2), "TopBorderDistance"));
}

DECLARE_RTFEXPORT_TEST(testTdf106950, "tdf106950.rtf")
{
    uno::Reference<text::XTextRange> xPara(getParagraph(1));
    // This was ParagraphAdjust_LEFT, trying to set CharShadingValue on a
    // paragraph style thrown an exception, and remaining properties were not
    // set.
    CPPUNIT_ASSERT_EQUAL(
        style::ParagraphAdjust_CENTER,
        static_cast<style::ParagraphAdjust>(getProperty<sal_Int16>(xPara, "ParaAdjust")));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf116371)
{
    loadAndReload("tdf116371.odt");
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    auto xShape(getShape(1));
    // Without the accompanying fix in place, this test would have failed with
    // 'Unknown property: RotateAngle', i.e. export lost the rotation, and then
    // import created a Writer picture (instead of a Draw one).
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4700.0, getProperty<double>(xShape, "RotateAngle"), 10);
}

DECLARE_RTFEXPORT_TEST(testTdf133437, "tdf133437.rtf")
{
    CPPUNIT_ASSERT_EQUAL(3, getPages());
    CPPUNIT_ASSERT_EQUAL(560, getShapes()); // 285 \shp + 275 \poswX

    xmlDocUniquePtr pDump = parseLayoutDump();
    // Count shapes on first page
    assertXPath(pDump, "/root/page[1]/body/txt[1]/anchored/SwAnchoredDrawObject", 79);

    // Second page
    assertXPath(pDump, "/root/page[2]/body/txt[2]/anchored/SwAnchoredDrawObject", 118);

    // Third page
    assertXPath(pDump, "/root/page[3]/body/txt[2]/anchored/SwAnchoredDrawObject", 84);
}

CPPUNIT_TEST_FIXTURE(Test, testTdf128320)
{
    loadAndReload("tdf128320.odt");
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    // Shape does exist in RTF output
    auto xShape(getShape(1));
    CPPUNIT_ASSERT(xShape.is());

    // Let's see what is inside output RTF file
    SvStream* pStream = maTempFile.GetStream(StreamMode::READ);
    CPPUNIT_ASSERT(pStream);
    OString aRtfContent(read_uInt8s_ToOString(*pStream, pStream->TellEnd()));

    // There are some RTF tokens for shape props
    // They are much more inside, but let's use \shpwr2 as an indicator
    sal_Int32 nPos = aRtfContent.indexOf("\\shpwr2", 0);
    CPPUNIT_ASSERT(nPos > 0);

    // It goes AFTER shape instruction (sadly here we do not check if it is contained inside)
    sal_Int32 nPosShp = aRtfContent.indexOf("\\shpinst", 0);
    CPPUNIT_ASSERT(nPosShp > 0);

    // But there are no more shape properties!
    nPos = aRtfContent.indexOf("\\shpwr2", nPos + 1);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-1), nPos);
}

DECLARE_RTFEXPORT_TEST(testTdf129513, "tdf129513.rtf")
{
    // \pagebb after \intbl must not reset the "in table" flag
    CPPUNIT_ASSERT_EQUAL(2, getParagraphs());
    // Make sure the first paragraph is imported in table
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(1), uno::UNO_QUERY_THROW);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable->getCellNames().getLength());
    uno::Reference<text::XText> xCell(xTable->getCellByName("A1"), uno::UNO_QUERY_THROW);
    CPPUNIT_ASSERT_EQUAL(OUString("In table"), xCell->getString());
}

DECLARE_RTFEXPORT_TEST(testTdf138210, "tdf138210.rtf")
{
    uno::Reference<text::XTextFramesSupplier> xTextFramesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xIndexAccess(xTextFramesSupplier->getTextFrames(),
                                                         uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xIndexAccess->getCount());
}

CPPUNIT_TEST_FIXTURE(Test, testTdf137894)
{
    loadAndReload("tdf137894.odt");
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    lang::Locale locale1(getProperty<lang::Locale>(getRun(getParagraph(1), 1), "CharLocaleAsian"));
    CPPUNIT_ASSERT_EQUAL(OUString("ja"), locale1.Language);
    CPPUNIT_ASSERT_EQUAL(OUString("MS UI Gothic"),
                         getProperty<OUString>(getRun(getParagraph(1), 1), "CharFontNameAsian"));
    CPPUNIT_ASSERT_EQUAL(20.f, getProperty<float>(getRun(getParagraph(1), 1), "CharHeightAsian"));
    CPPUNIT_ASSERT_EQUAL(OUString("Mangal"),
                         getProperty<OUString>(getRun(getParagraph(1), 1), "CharFontNameComplex"));
    CPPUNIT_ASSERT_EQUAL(20.f, getProperty<float>(getRun(getParagraph(1), 1), "CharHeightComplex"));
    lang::Locale locale2(
        getProperty<lang::Locale>(getRun(getParagraph(2), 1), "CharLocaleComplex"));
    CPPUNIT_ASSERT_EQUAL(OUString("he"), locale2.Language);
    CPPUNIT_ASSERT_EQUAL(32.f, getProperty<float>(getRun(getParagraph(2), 1), "CharHeightComplex"));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf138779)
{
    loadAndReload("tdf138779.docx");
    // The text "2. Kozuka Mincho Pro, 8 pt Ruby ..." has font size 11pt ( was 20pt ).
    CPPUNIT_ASSERT_EQUAL(11.f, getProperty<float>(getRun(getParagraph(2), 14), "CharHeight"));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf144437)
{
    loadAndReload("tdf144437.odt");
    SvStream* pStream = maTempFile.GetStream(StreamMode::READ);
    CPPUNIT_ASSERT(pStream);
    OString aRtfContent(read_uInt8s_ToOString(*pStream, pStream->TellEnd()));

    sal_Int32 nTextEndPos = aRtfContent.indexOf("Bookmark here->", 0) + 14;
    CPPUNIT_ASSERT_MESSAGE("Para content wasn't found in file", nTextEndPos > 0);

    sal_Int32 nBmkStartPos = aRtfContent.indexOf("{\\*\\bkmkstart bookmark}", 0);
    CPPUNIT_ASSERT_MESSAGE("Bookmark start wasn't found in file", nBmkStartPos > 0);

    sal_Int32 nBmkEndPos = aRtfContent.indexOf("{\\*\\bkmkend bookmark}", 0);
    CPPUNIT_ASSERT_MESSAGE("Bookmark end wasn't found in file", nBmkEndPos > 0);

    CPPUNIT_ASSERT_MESSAGE("Bookmark started in wrong position", nBmkStartPos > nTextEndPos);
    CPPUNIT_ASSERT_MESSAGE("Bookmark ended in wrong position", nBmkEndPos > nTextEndPos);
    CPPUNIT_ASSERT_MESSAGE("Bookmark start & end are wrong", nBmkEndPos > nBmkStartPos);
}

DECLARE_RTFEXPORT_TEST(testTdf131234, "tdf131234.rtf")
{
    uno::Reference<text::XTextRange> xRun = getRun(getParagraph(1), 1, OUString(u"Hello"));

    // Ensure that text has default font attrs in spite of style referenced
    // E.g. 12pt, Times New Roman, black, no bold, no italic, no underline
    CPPUNIT_ASSERT_EQUAL(12.f, getProperty<float>(xRun, "CharHeight"));
    CPPUNIT_ASSERT_EQUAL(COL_BLACK, getProperty<Color>(xRun, "CharColor"));
    CPPUNIT_ASSERT_EQUAL(OUString("Times New Roman"), getProperty<OUString>(xRun, "CharFontName"));
    CPPUNIT_ASSERT_EQUAL(awt::FontWeight::NORMAL, getProperty<float>(xRun, "CharWeight"));
    CPPUNIT_ASSERT_EQUAL(awt::FontUnderline::NONE, getProperty<sal_Int16>(xRun, "CharUnderline"));
    CPPUNIT_ASSERT_EQUAL(awt::FontSlant_NONE, getProperty<awt::FontSlant>(xRun, "CharPosture"));
}

DECLARE_RTFEXPORT_TEST(testTdf118047, "tdf118047.rtf")
{
    uno::Reference<text::XTextRange> xPara = getParagraph(1);

    // Ensure that default "Normal" style properties are not applied to text:
    // text remains with fontsize 12pt and no huge margin below
    CPPUNIT_ASSERT_EQUAL(12.f, getProperty<float>(getRun(xPara, 1), "CharHeight"));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), getProperty<sal_Int32>(getParagraph(1), "ParaBottomMargin"));

    // Same for header, it should not derive props from "Normal" style
    CPPUNIT_ASSERT_EQUAL(OUString("Header"), parseDump("/root/page[1]/header/txt/text()"));
    sal_Int32 nHeight = parseDump("/root/page[1]/header/infos/bounds", "height").toInt32();
    CPPUNIT_ASSERT_MESSAGE("Header is too large", 1000 > nHeight);
}

DECLARE_RTFEXPORT_TEST(testTdf104390, "tdf104390.rtf")
{
    uno::Reference<text::XTextRange> xPara = getParagraph(1);
    uno::Reference<container::XEnumerationAccess> xRunEnumAccess(xPara, uno::UNO_QUERY);
    uno::Reference<container::XEnumeration> xRunEnum = xRunEnumAccess->createEnumeration();

    // Check font in first run
    uno::Reference<text::XTextRange> xRun(xRunEnum->nextElement(), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(36.f, getProperty<float>(xRun, "CharHeight"));
    CPPUNIT_ASSERT_EQUAL(OUString("Courier New"), getProperty<OUString>(xRun, "CharFontName"));

    // Ensure there is only one run
    CPPUNIT_ASSERT_MESSAGE("Extra elements in paragraph", !xRunEnum->hasMoreElements());
}

DECLARE_RTFEXPORT_TEST(testTdf153681, "tdf153681.odt")
{
    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY_THROW);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY_THROW);

    // This is outside table
    uno::Reference<text::XTextTable> xTable(xTables->getByIndex(1), uno::UNO_QUERY_THROW);
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 2
    // - Actual  : 3
    // Generates extra cell
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable->getColumns()->getCount());
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
