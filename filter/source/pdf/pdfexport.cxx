/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */


#include <osl/file.hxx>
#include <tools/debug.hxx>
#include <tools/urlobj.hxx>
#include <tools/poly.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <utility>
#include <vcl/canvastools.hxx>
#include <vcl/mapmod.hxx>
#include <vcl/gdimtf.hxx>
#include <rtl/ustring.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/string.hxx>
#include <comphelper/storagehelper.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <toolkit/awt/vclxdevice.hxx>
#include <unotools/configmgr.hxx>
#include <comphelper/compbase.hxx>
#include <officecfg/Office/Common.hxx>

#include "pdfexport.hxx"
#include <strings.hrc>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/ModuleManager.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/view/XViewSettingsSupplier.hpp>
#include <com/sun/star/task/XInteractionRequest.hpp>
#include <com/sun/star/task/PDFExportException.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/security/XCertificate.hpp>
#include <com/sun/star/beans/XMaterialHolder.hpp>
#include <com/sun/star/xml/crypto/SEInitializer.hpp>

#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::view;
using namespace ::com::sun::star::graphic;


PDFExport::PDFExport( const Reference< XComponent >& rxSrcDoc,
                      const Reference< task::XStatusIndicator >& rxStatusIndicator,
                      const Reference< task::XInteractionHandler >& rxIH,
                      const Reference< XComponentContext >& xContext ) :
    mxSrcDoc                    ( rxSrcDoc ),
    mxContext                   ( xContext ),
    mxStatusIndicator           ( rxStatusIndicator ),
    mxIH                        ( rxIH ),
    mbUseTaggedPDF              ( false ),
    mnPDFTypeSelection          ( 0 ),
    mbPDFUACompliance           ( false),
    mbExportNotes               ( true ),
    mbExportNotesInMargin       ( false ),
    mbExportPlaceholders        ( false ),
    mbUseReferenceXObject       ( false ),
    mbExportNotesPages          ( false ),
    mbExportOnlyNotesPages      ( false ),
    mbUseTransitionEffects      ( true ),
    mbExportBookmarks           ( true ),
    mbExportHiddenSlides        ( false ),
    mbSinglePageSheets          ( false ),
    mnOpenBookmarkLevels        ( -1 ),
    mbUseLosslessCompression    ( false ),
    mbReduceImageResolution     ( true ),
    mbSkipEmptyPages            ( true ),
    mbAddStream                 ( false ),
    mnMaxImageResolution        ( 300 ),
    mnQuality                   ( 80 ),
    mnFormsFormat               ( 0 ),
    mbExportFormFields          ( true ),
    mbAllowDuplicateFieldNames  ( false ),
    mnProgressValue             ( 0 ),
    mbRemoveTransparencies      ( false ),

    mbIsRedactMode              ( false ),
    maWatermarkColor            ( COL_LIGHTGREEN ),
    maWatermarkFontName         ( "Helvetica" ),

    mbHideViewerToolbar         ( false ),
    mbHideViewerMenubar         ( false ),
    mbHideViewerWindowControls  ( false ),
    mbFitWindow                 ( false ),
    mbCenterWindow              ( false ),
    mbOpenInFullScreenMode      ( false ),
    mbDisplayPDFDocumentTitle   ( true ),
    mnPDFDocumentMode           ( 0 ),
    mnPDFDocumentAction         ( 0 ),
    mnZoom                      ( 100 ),
    mnInitialPage               ( 1 ),
    mnPDFPageLayout             ( 0 ),

    mbEncrypt                   ( false ),
    mbRestrictPermissions       ( false ),
    mnPrintAllowed              ( 2 ),
    mnChangesAllowed            ( 4 ),
    mbCanCopyOrExtract          ( true ),
    mbCanExtractForAccessibility( true ),

    // #i56629
    mbExportRelativeFsysLinks       ( false ),
    mnDefaultLinkAction         ( 0 ),
    mbConvertOOoTargetToPDFTarget( false ),
    mbExportBmkToDest           ( false ),
    mbSignPDF                   ( false )
{
}


PDFExport::~PDFExport()
{
}


bool PDFExport::ExportSelection( vcl::PDFWriter& rPDFWriter,
    Reference< css::view::XRenderable > const & rRenderable,
    const Any& rSelection,
    const StringRangeEnumerator& rRangeEnum,
    Sequence< PropertyValue >& rRenderOptions,
    sal_Int32 nPageCount )
{
    bool        bRet = false;
    try
    {
        Any* pFirstPage = nullptr;
        Any* pLastPage = nullptr;

        bool bExportNotesPages = false;

        auto rRenderOptionsRange = asNonConstRange(rRenderOptions);
        for( sal_Int32 nData = 0, nDataCount = rRenderOptions.getLength(); nData < nDataCount; ++nData )
        {
            if ( rRenderOptions[ nData ].Name == "IsFirstPage" )
                pFirstPage = &rRenderOptionsRange[ nData ].Value;
            else if ( rRenderOptions[ nData ].Name == "IsLastPage" )
                pLastPage = &rRenderOptionsRange[ nData ].Value;
            else if ( rRenderOptions[ nData ].Name == "ExportNotesPages" )
                rRenderOptionsRange[ nData ].Value >>= bExportNotesPages;
        }

        OutputDevice* pOut = rPDFWriter.GetReferenceDevice();

        if( pOut )
        {
            if ( nPageCount )
            {
                vcl::PDFExtOutDevData& rPDFExtOutDevData = dynamic_cast<vcl::PDFExtOutDevData&>(*pOut->GetExtOutDevData());
                rPDFExtOutDevData.SetIsExportNotesPages( bExportNotesPages );

                sal_Int32 nCurrentPage(0);
                StringRangeEnumerator::Iterator aIter = rRangeEnum.begin();
                StringRangeEnumerator::Iterator aEnd  = rRangeEnum.end();
                while ( aIter != aEnd )
                {
                    const Sequence< PropertyValue > aRenderer( rRenderable->getRenderer( *aIter, rSelection, rRenderOptions ) );
                    awt::Size                   aPageSize;

                    for( const PropertyValue& rProp : aRenderer )
                    {
                        if ( rProp.Name == "PageSize" )
                        {
                            rProp.Value >>= aPageSize;
                            break;
                        }
                    }

                    rPDFExtOutDevData.SetCurrentPageNumber( nCurrentPage );

                    GDIMetaFile                 aMtf;
                    const MapMode               aMapMode( MapUnit::Map100thMM );
                    const Size                  aMtfSize( aPageSize.Width, aPageSize.Height );

                    pOut->Push();
                    pOut->EnableOutput( false );
                    pOut->SetMapMode( aMapMode );

                    aMtf.SetPrefSize( aMtfSize );
                    aMtf.SetPrefMapMode( aMapMode );
                    aMtf.Record( pOut );

                    // #i35176#
                    // IsLastPage property.
                    const sal_Int32 nCurrentRenderer = *aIter;
                    ++aIter;
                    if ( pLastPage && aIter == aEnd )
                        *pLastPage <<= true;

                    rRenderable->render( nCurrentRenderer, rSelection, rRenderOptions );

                    aMtf.Stop();
                    aMtf.WindStart();

                    bool bEmptyPage = false;
                    if( aMtf.GetActionSize() &&
                             ( !mbSkipEmptyPages || aPageSize.Width || aPageSize.Height ) )
                    {
                        // We convert the whole metafile into a bitmap to get rid of the
                        // text covered by redaction shapes
                        if (mbIsRedactMode)
                        {
                            try
                            {
                                Graphic aGraph(aMtf);
                                // use antialiasing to improve how graphic objects look
                                BitmapEx bmp = aGraph.GetBitmapEx(GraphicConversionParameters(Size(0, 0), false, true, false));
                                Graphic bgraph(bmp);
                                aMtf = bgraph.GetGDIMetaFile();
                            }
                            catch(const Exception&)
                            {
                                TOOLS_WARN_EXCEPTION("filter.pdf", "Something went wrong while converting metafile to bitmap");
                            }
                        }

                        ImplExportPage(rPDFWriter, rPDFExtOutDevData, aMtf);
                        bRet = true;
                    }
                    else
                    {
                        bEmptyPage = true;
                    }

                    pOut->Pop();

                    if ( mxStatusIndicator.is() )
                        mxStatusIndicator->setValue( mnProgressValue );
                    if ( pFirstPage )
                        *pFirstPage <<= false;

                    ++mnProgressValue;
                    if (!bEmptyPage)
                    {
                        // Calculate the page number in the PDF output, which may be smaller than the page number in
                        // case of hidden slides or a partial export.
                        ++nCurrentPage;
                    }
                }
            }
            else
            {
                bRet = true;                            // #i18334# nPageCount == 0,
                rPDFWriter.NewPage( 10000, 10000 );     // creating dummy page
                rPDFWriter.SetMapMode(MapMode(MapUnit::Map100thMM));
            }
        }
    }
    catch(const RuntimeException &)
    {
    }
    return bRet;
}

namespace {

class PDFExportStreamDoc : public vcl::PDFOutputStream
{
private:

    Reference< XComponent >             m_xSrcDoc;
    Sequence< beans::NamedValue >       m_aPreparedPassword;

public:

    PDFExportStreamDoc( const Reference< XComponent >& xDoc, const Sequence<beans::NamedValue>& rPwd )
    : m_xSrcDoc( xDoc ),
      m_aPreparedPassword( rPwd )
    {}

    virtual void write( const Reference< XOutputStream >& xStream ) override;
};

}

void PDFExportStreamDoc::write( const Reference< XOutputStream >& xStream )
{
    Reference< css::frame::XStorable > xStore( m_xSrcDoc, UNO_QUERY );
    if( !xStore.is() )
        return;

    std::vector<beans::PropertyValue> aArgs {
        comphelper::makePropertyValue("FilterName", OUString()),
        comphelper::makePropertyValue("OutputStream", xStream),
    };
    if (m_aPreparedPassword.hasElements())
        aArgs.push_back(comphelper::makePropertyValue("EncryptionData", m_aPreparedPassword));

    try
    {
        xStore->storeToURL("private:stream", comphelper::containerToSequence(aArgs));
    }
    catch( const IOException& )
    {
    }
}


static OUString getMimetypeForDocument( const Reference< XComponentContext >& xContext,
                                        const Reference< XComponent >& xDoc ) noexcept
{
    OUString aDocMimetype;
    try
    {
        // get document service name
        Reference< css::frame::XStorable > xStore( xDoc, UNO_QUERY );
        Reference< frame::XModuleManager2 > xModuleManager = frame::ModuleManager::create(xContext);
        if( xStore.is() )
        {
            OUString aDocServiceName = xModuleManager->identify( Reference< XInterface >( xStore, uno::UNO_QUERY ) );
            if ( !aDocServiceName.isEmpty() )
            {
                // get the actual filter name
                Reference< lang::XMultiServiceFactory > xConfigProvider =
                    configuration::theDefaultProvider::get( xContext );
                beans::NamedValue aPathProp;
                aPathProp.Name = "nodepath";
                aPathProp.Value <<= OUString( "/org.openoffice.Setup/Office/Factories/" );
                uno::Sequence< uno::Any > aArgs{ uno::Any(aPathProp) };

                Reference< container::XNameAccess > xSOFConfig(
                    xConfigProvider->createInstanceWithArguments(
                        "com.sun.star.configuration.ConfigurationAccess", aArgs ),
                    uno::UNO_QUERY );

                Reference< container::XNameAccess > xApplConfig;
                xSOFConfig->getByName( aDocServiceName ) >>= xApplConfig;
                if ( xApplConfig.is() )
                {
                    OUString aFilterName;
                    xApplConfig->getByName( "ooSetupFactoryActualFilter" ) >>= aFilterName;
                    if( !aFilterName.isEmpty() )
                    {
                        // find the related type name
                        OUString aTypeName;
                        Reference< container::XNameAccess > xFilterFactory(
                            xContext->getServiceManager()->createInstanceWithContext("com.sun.star.document.FilterFactory", xContext),
                            uno::UNO_QUERY );

                        Sequence< beans::PropertyValue > aFilterData;
                        xFilterFactory->getByName( aFilterName ) >>= aFilterData;
                        for ( const beans::PropertyValue& rProp : std::as_const(aFilterData) )
                            if ( rProp.Name == "Type" )
                                rProp.Value >>= aTypeName;

                        if ( !aTypeName.isEmpty() )
                        {
                            // find the mediatype
                            Reference< container::XNameAccess > xTypeDetection(
                                xContext->getServiceManager()->createInstanceWithContext("com.sun.star.document.TypeDetection", xContext),
                                UNO_QUERY );

                            Sequence< beans::PropertyValue > aTypeData;
                            xTypeDetection->getByName( aTypeName ) >>= aTypeData;
                            for ( const beans::PropertyValue& rProp : std::as_const(aTypeData) )
                                if ( rProp.Name == "MediaType" )
                                    rProp.Value >>= aDocMimetype;
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
    }
    return aDocMimetype;
}

uno::Reference<security::XCertificate>
PDFExport::GetCertificateFromSubjectName(const std::u16string_view& rSubjectName) const
{
    uno::Reference<xml::crypto::XSEInitializer> xSEInitializer
        = xml::crypto::SEInitializer::create(mxContext);
    uno::Reference<xml::crypto::XXMLSecurityContext> xSecurityContext
        = xSEInitializer->createSecurityContext(OUString());
    uno::Reference<xml::crypto::XSecurityEnvironment> xSecurityEnvironment
        = xSecurityContext->getSecurityEnvironment();
    for (const auto& xCertificate : xSecurityEnvironment->getPersonalCertificates())
    {
        if (xCertificate->getSubjectName() == rSubjectName)
        {
            return xCertificate;
        }
    }

    return {};
}

bool PDFExport::Export( const OUString& rFile, const Sequence< PropertyValue >& rFilterData )
{
    INetURLObject   aURL( rFile );
    bool        bRet = false;

    std::set< vcl::PDFWriter::ErrorCode > aErrors;

    if( aURL.GetProtocol() != INetProtocol::File )
    {
        OUString aTmp;

        if( osl::FileBase::getFileURLFromSystemPath( rFile, aTmp ) == osl::FileBase::E_None )
            aURL = INetURLObject(aTmp);
    }

    if( aURL.GetProtocol() == INetProtocol::File )
    {
        Reference< XRenderable > xRenderable( mxSrcDoc, UNO_QUERY );

        if( xRenderable.is() )
        {
            rtl::Reference<VCLXDevice>  xDevice(new VCLXDevice);
            OUString                    aPageRange;
            Any                         aSelection;
            vcl::PDFWriter::PDFWriterContext aContext;
            OUString aOpenPassword, aPermissionPassword;
            Reference< beans::XMaterialHolder > xEnc;
            Sequence< beans::NamedValue > aPreparedPermissionPassword;


            // getting the string for the creator
            OUString aCreator;
            Reference< XServiceInfo > xInfo( mxSrcDoc, UNO_QUERY );
            if ( xInfo.is() )
            {
                if ( xInfo->supportsService( "com.sun.star.presentation.PresentationDocument" ) )
                    aCreator += "Impress";
                else if ( xInfo->supportsService( "com.sun.star.drawing.DrawingDocument" ) )
                    aCreator += "Draw";
                else if ( xInfo->supportsService( "com.sun.star.text.TextDocument" ) )
                    aCreator += "Writer";
                else if ( xInfo->supportsService( "com.sun.star.sheet.SpreadsheetDocument" ) )
                    aCreator += "Calc";
                else if ( xInfo->supportsService( "com.sun.star.formula.FormulaProperties"  ) )
                    aCreator += "Math";
            }

            Reference< document::XDocumentPropertiesSupplier > xDocumentPropsSupplier( mxSrcDoc, UNO_QUERY );
            if ( xDocumentPropsSupplier.is() )
            {
                Reference< document::XDocumentProperties > xDocumentProps( xDocumentPropsSupplier->getDocumentProperties() );
                if ( xDocumentProps.is() )
                {
                    aContext.DocumentInfo.Title = xDocumentProps->getTitle();
                    aContext.DocumentInfo.Author = xDocumentProps->getAuthor();
                    aContext.DocumentInfo.Subject = xDocumentProps->getSubject();
                    aContext.DocumentInfo.Keywords = ::comphelper::string::convertCommaSeparated(xDocumentProps->getKeywords());
                }
            }

            if (!utl::ConfigManager::IsFuzzing())
            {
                // getting the string for the producer
                OUString aProducerOverride = officecfg::Office::Common::Save::Document::GeneratorOverride::get();
                if (!aProducerOverride.isEmpty())
                    aContext.DocumentInfo.Producer = aProducerOverride;
                else
                    aContext.DocumentInfo.Producer =
                        utl::ConfigManager::getProductName() +
                        " " +
                        utl::ConfigManager::getProductVersion();
            }

            aContext.DocumentInfo.Creator = aCreator;

            OUString aSignCertificateSubjectName;
            for ( const beans::PropertyValue& rProp : rFilterData )
            {
                if ( rProp.Name == "PageRange" )
                    rProp.Value >>= aPageRange;
                else if ( rProp.Name == "Selection" )
                    aSelection = rProp.Value;
                else if ( rProp.Name == "UseLosslessCompression" )
                    rProp.Value >>= mbUseLosslessCompression;
                else if ( rProp.Name == "Quality" )
                    rProp.Value >>= mnQuality;
                else if ( rProp.Name == "ReduceImageResolution" )
                    rProp.Value >>= mbReduceImageResolution;
                else if ( rProp.Name == "IsSkipEmptyPages" )
                    rProp.Value >>= mbSkipEmptyPages;
                else if ( rProp.Name == "MaxImageResolution" )
                    rProp.Value >>= mnMaxImageResolution;
                else if ( rProp.Name == "UseTaggedPDF" )
                    rProp.Value >>= mbUseTaggedPDF;
                else if ( rProp.Name == "SelectPdfVersion" )
                    rProp.Value >>= mnPDFTypeSelection;
                else if ( rProp.Name == "PDFUACompliance" )
                    rProp.Value >>= mbPDFUACompliance;
                else if ( rProp.Name == "ExportNotes" )
                    rProp.Value >>= mbExportNotes;
                else if ( rProp.Name == "ExportNotesInMargin" )
                    rProp.Value >>= mbExportNotesInMargin;
                else if ( rProp.Name == "ExportNotesPages" )
                    rProp.Value >>= mbExportNotesPages;
                else if ( rProp.Name == "ExportOnlyNotesPages" )
                    rProp.Value >>= mbExportOnlyNotesPages;
                else if ( rProp.Name == "UseTransitionEffects" )
                    rProp.Value >>= mbUseTransitionEffects;
                else if ( rProp.Name == "ExportFormFields" )
                    rProp.Value >>= mbExportFormFields;
                else if ( rProp.Name == "FormsType" )
                    rProp.Value >>= mnFormsFormat;
                else if ( rProp.Name == "AllowDuplicateFieldNames" )
                    rProp.Value >>= mbAllowDuplicateFieldNames;
                // viewer properties
                else if ( rProp.Name == "HideViewerToolbar" )
                    rProp.Value >>= mbHideViewerToolbar;
                else if ( rProp.Name == "HideViewerMenubar" )
                    rProp.Value >>= mbHideViewerMenubar;
                else if ( rProp.Name == "HideViewerWindowControls" )
                    rProp.Value >>= mbHideViewerWindowControls;
                else if ( rProp.Name == "ResizeWindowToInitialPage" )
                    rProp.Value >>= mbFitWindow;
                else if ( rProp.Name == "CenterWindow" )
                    rProp.Value >>= mbCenterWindow;
                else if ( rProp.Name == "OpenInFullScreenMode" )
                    rProp.Value >>= mbOpenInFullScreenMode;
                else if ( rProp.Name == "DisplayPDFDocumentTitle" )
                    rProp.Value >>= mbDisplayPDFDocumentTitle;
                else if ( rProp.Name == "InitialView" )
                    rProp.Value >>= mnPDFDocumentMode;
                else if ( rProp.Name == "Magnification" )
                    rProp.Value >>= mnPDFDocumentAction;
                else if ( rProp.Name == "Zoom" )
                    rProp.Value >>= mnZoom;
                else if ( rProp.Name == "InitialPage" )
                    rProp.Value >>= mnInitialPage;
                else if ( rProp.Name == "PageLayout" )
                    rProp.Value >>= mnPDFPageLayout;
                else if ( rProp.Name == "FirstPageOnLeft" )
                    rProp.Value >>= aContext.FirstPageLeft;
                else if ( rProp.Name == "IsAddStream" )
                    rProp.Value >>= mbAddStream;
                else if ( rProp.Name == "Watermark" )
                    rProp.Value >>= msWatermark;
                else if ( rProp.Name == "WatermarkColor" )
                {
                    sal_Int32 nColor{};
                    if (rProp.Value >>= nColor)
                    {
                        maWatermarkColor = Color(ColorTransparency, nColor);
                    }
                }
                else if (rProp.Name == "WatermarkFontHeight")
                {
                    sal_Int32 nFontHeight{};
                    if (rProp.Value >>= nFontHeight)
                    {
                        moWatermarkFontHeight = nFontHeight;
                    }
                }
                else if (rProp.Name == "WatermarkRotateAngle")
                {
                    sal_Int32 nRotateAngle{};
                    if (rProp.Value >>= nRotateAngle)
                    {
                        moWatermarkRotateAngle = Degree10(nRotateAngle);
                    }
                }
                else if (rProp.Name == "WatermarkFontName")
                {
                    OUString aFontName{};
                    if (rProp.Value >>= aFontName)
                    {
                        maWatermarkFontName = aFontName;
                    }
                }
                else if ( rProp.Name == "TiledWatermark" )
                    rProp.Value >>= msTiledWatermark;
                // now all the security related properties...
                else if ( rProp.Name == "EncryptFile" )
                    rProp.Value >>= mbEncrypt;
                else if ( rProp.Name == "DocumentOpenPassword" )
                    rProp.Value >>= aOpenPassword;
                else if ( rProp.Name == "RestrictPermissions" )
                    rProp.Value >>= mbRestrictPermissions;
                else if ( rProp.Name == "PermissionPassword" )
                    rProp.Value >>= aPermissionPassword;
                else if ( rProp.Name == "PreparedPasswords" )
                    rProp.Value >>= xEnc;
                else if ( rProp.Name == "PreparedPermissionPassword" )
                    rProp.Value >>= aPreparedPermissionPassword;
                else if ( rProp.Name == "Printing" )
                    rProp.Value >>= mnPrintAllowed;
                else if ( rProp.Name == "Changes" )
                    rProp.Value >>= mnChangesAllowed;
                else if ( rProp.Name == "EnableCopyingOfContent" )
                    rProp.Value >>= mbCanCopyOrExtract;
                else if ( rProp.Name == "EnableTextAccessForAccessibilityTools" )
                    rProp.Value >>= mbCanExtractForAccessibility;
                // i56629 links extra (relative links and other related stuff)
                else if ( rProp.Name == "ExportLinksRelativeFsys" )
                    rProp.Value >>= mbExportRelativeFsysLinks;
                else if ( rProp.Name == "PDFViewSelection" )
                    rProp.Value >>= mnDefaultLinkAction;
                else if ( rProp.Name == "ConvertOOoTargetToPDFTarget" )
                    rProp.Value >>= mbConvertOOoTargetToPDFTarget;
                else if ( rProp.Name == "ExportBookmarksToPDFDestination" )
                    rProp.Value >>= mbExportBmkToDest;
                else if ( rProp.Name == "ExportBookmarks" )
                    rProp.Value >>= mbExportBookmarks;
                else if ( rProp.Name == "ExportHiddenSlides" )
                    rProp.Value >>= mbExportHiddenSlides;
                else if ( rProp.Name == "SinglePageSheets" )
                    rProp.Value >>= mbSinglePageSheets;
                else if ( rProp.Name == "OpenBookmarkLevels" )
                    rProp.Value >>= mnOpenBookmarkLevels;
                else if ( rProp.Name == "SignPDF" )
                    rProp.Value >>= mbSignPDF;
                else if ( rProp.Name == "SignatureLocation" )
                    rProp.Value >>= msSignLocation;
                else if ( rProp.Name == "SignatureReason" )
                    rProp.Value >>= msSignReason;
                else if ( rProp.Name == "SignatureContactInfo" )
                    rProp.Value >>= msSignContact;
                else if ( rProp.Name == "SignaturePassword" )
                    rProp.Value >>= msSignPassword;
                else if ( rProp.Name == "SignatureCertificate" )
                    rProp.Value >>= maSignCertificate;
                else if (rProp.Name == "SignCertificateSubjectName")
                    rProp.Value >>= aSignCertificateSubjectName;
                else if ( rProp.Name == "SignatureTSA" )
                    rProp.Value >>= msSignTSA;
                else if ( rProp.Name == "ExportPlaceholders" )
                    rProp.Value >>= mbExportPlaceholders;
                else if ( rProp.Name == "UseReferenceXObject" )
                    rProp.Value >>= mbUseReferenceXObject;
                // Redaction & bitmap related stuff
                else if ( rProp.Name == "IsRedactMode" )
                    rProp.Value >>= mbIsRedactMode;
            }

            if (!maSignCertificate.is() && !aSignCertificateSubjectName.isEmpty())
            {
                maSignCertificate = GetCertificateFromSubjectName(aSignCertificateSubjectName);
            }

            aContext.URL        = aURL.GetMainURL(INetURLObject::DecodeMechanism::ToIUri);

            // set the correct version, depending on user request
            switch( mnPDFTypeSelection )
            {
            default:
            case 0:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_7;
                break;
            case 1:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_1;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = true;  // does not allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 2:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_2;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = false; // does allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 3:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_3;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = false; // does allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 15:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_5;
                break;
            case 16:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_6;
                break;
            case 17:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_7;
                break;
            }

            // PDF/UA support
            aContext.UniversalAccessibilityCompliance = mbPDFUACompliance;
            if (mbPDFUACompliance)
            {
                // ISO 14289-1:2014, Clause: 7.1
                mbUseTaggedPDF = true;
                // ISO 14289-1:2014, Clause: 7.16
                mbCanExtractForAccessibility = true;
                // ISO 14289-1:2014, Clause: 7.20
                mbUseReferenceXObject = false;
            }

            // copy in context the values default in the constructor or set by the FilterData sequence of properties
            aContext.Tagged     = mbUseTaggedPDF;

            // values used in viewer
            aContext.HideViewerToolbar          = mbHideViewerToolbar;
            aContext.HideViewerMenubar          = mbHideViewerMenubar;
            aContext.HideViewerWindowControls   = mbHideViewerWindowControls;
            aContext.FitWindow                  = mbFitWindow;
            aContext.CenterWindow               = mbCenterWindow;
            aContext.OpenInFullScreenMode       = mbOpenInFullScreenMode;
            aContext.DisplayPDFDocumentTitle    = mbDisplayPDFDocumentTitle;
            aContext.InitialPage                = mnInitialPage-1;
            aContext.OpenBookmarkLevels         = mnOpenBookmarkLevels;

            switch( mnPDFDocumentMode )
            {
                default:
                case 0:
                    aContext.PDFDocumentMode = vcl::PDFWriter::ModeDefault;
                    break;
                case 1:
                    aContext.PDFDocumentMode = vcl::PDFWriter::UseOutlines;
                    break;
                case 2:
                    aContext.PDFDocumentMode = vcl::PDFWriter::UseThumbs;
                    break;
            }
            switch( mnPDFDocumentAction )
            {
                default:
                case 0:
                    aContext.PDFDocumentAction = vcl::PDFWriter::ActionDefault;
                    break;
                case 1:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitInWindow;
                    break;
                case 2:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitWidth;
                    break;
                case 3:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitVisible;
                    break;
                case 4:
                    aContext.PDFDocumentAction = vcl::PDFWriter::ActionZoom;
                    aContext.Zoom = mnZoom;
                    break;
            }

            switch( mnPDFPageLayout )
            {
                default:
                case 0:
                    aContext.PageLayout = vcl::PDFWriter::DefaultLayout;
                    break;
                case 1:
                    aContext.PageLayout = vcl::PDFWriter::SinglePage;
                    break;
                case 2:
                    aContext.PageLayout = vcl::PDFWriter::Continuous;
                    break;
                case 3:
                    aContext.PageLayout = vcl::PDFWriter::ContinuousFacing;
                    break;
            }

            aContext.FirstPageLeft = false;

            // check if PDF/A, which does not allow encryption
            if( aContext.Version != vcl::PDFWriter::PDFVersion::PDF_A_1 )
            {
                // set check for permission change password
                // if not enabled and no permission password, force permissions to default as if PDF where without encryption
                if( mbRestrictPermissions && (xEnc.is() || !aPermissionPassword.isEmpty()) )
                {
                    mbEncrypt = true; // permission set as desired, done after
                }
                else
                {
                    // force permission to default
                    mnPrintAllowed                  = 2 ;
                    mnChangesAllowed                = 4 ;
                    mbCanCopyOrExtract              = true;
                    mbCanExtractForAccessibility    = true ;
                }

                switch( mnPrintAllowed )
                {
                case 0: // initialized when aContext is build, means no printing
                    break;
                default:
                case 2:
                    aContext.Encryption.CanPrintFull            = true;
                    [[fallthrough]];
                case 1:
                    aContext.Encryption.CanPrintTheDocument     = true;
                    break;
                }

                switch( mnChangesAllowed )
                {
                case 0: // already in struct PDFSecPermissions CTOR
                    break;
                case 1:
                    aContext.Encryption.CanAssemble             = true;
                    break;
                case 2:
                    aContext.Encryption.CanFillInteractive      = true;
                    break;
                case 3:
                    aContext.Encryption.CanAddOrModify          = true;
                    break;
                default:
                case 4:
                    aContext.Encryption.CanModifyTheContent     =
                        aContext.Encryption.CanCopyOrExtract    =
                        aContext.Encryption.CanAddOrModify      =
                        aContext.Encryption.CanFillInteractive  = true;
                    break;
                }

                aContext.Encryption.CanCopyOrExtract                = mbCanCopyOrExtract;
                aContext.Encryption.CanExtractForAccessibility  = mbCanExtractForAccessibility;
                if( mbEncrypt && ! xEnc.is() )
                    xEnc = vcl::PDFWriter::InitEncryption( aPermissionPassword, aOpenPassword );
                if( mbEncrypt && !aPermissionPassword.isEmpty() && ! aPreparedPermissionPassword.hasElements() )
                    aPreparedPermissionPassword = comphelper::OStorageHelper::CreatePackageEncryptionData( aPermissionPassword );
            }
            // after this point we don't need the legacy clear passwords anymore
            // however they are still inside the passed filter data sequence
            // which is sadly out of our control
            aPermissionPassword.clear();
            aOpenPassword.clear();

            /*
            * FIXME: the entries are only implicitly defined by the resource file. Should there
            * ever be an additional form submit format this could get invalid.
            */
            switch( mnFormsFormat )
            {
                case 1:
                    aContext.SubmitFormat = vcl::PDFWriter::PDF;
                    break;
                case 2:
                    aContext.SubmitFormat = vcl::PDFWriter::HTML;
                    break;
                case 3:
                    aContext.SubmitFormat = vcl::PDFWriter::XML;
                    break;
                default:
                case 0:
                    aContext.SubmitFormat = vcl::PDFWriter::FDF;
                    break;
            }
            aContext.AllowDuplicateFieldNames = mbAllowDuplicateFieldNames;

            // get model
            Reference< frame::XModel > xModel( mxSrcDoc, UNO_QUERY );
            {
                // #i56629: Relative link stuff
                // set the base URL of the file: then base URL
                aContext.BaseURL = xModel->getURL();
                // relative link option is private to PDF Export filter and limited to local filesystem only
                aContext.RelFsys = mbExportRelativeFsysLinks;
                // determine the default action for PDF links
                switch( mnDefaultLinkAction )
                {
                default:
                    // default: URI, without fragment conversion (the bookmark in PDF may not work)
                case 0:
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIAction;
                    break;
                case 1:
                    // view PDF through the reader application
                    aContext.ForcePDFAction = true;
                    aContext.DefaultLinkAction = vcl::PDFWriter::LaunchAction;
                    break;
                case 2:
                    // view PDF through an Internet browser
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIActionDestination;
                    break;
                }
                aContext.ConvertOOoTargetToPDFTarget = mbConvertOOoTargetToPDFTarget;

                // check for Link Launch action, not allowed on PDF/A-1
                // this code chunk checks when the filter is called from scripting
                if( aContext.Version == vcl::PDFWriter::PDFVersion::PDF_A_1 &&
                    aContext.DefaultLinkAction == vcl::PDFWriter::LaunchAction )
                {
                    // force the similar allowed URI action
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIActionDestination;
                    // and remove the remote goto action forced on PDF file
                    aContext.ForcePDFAction = false;
                }
            }

            aContext.SignPDF = mbSignPDF;
            aContext.SignLocation = msSignLocation;
            aContext.SignContact = msSignContact;
            aContext.SignReason = msSignReason;
            aContext.SignPassword = msSignPassword;
            aContext.SignCertificate = maSignCertificate;
            aContext.SignTSA = msSignTSA;
            aContext.UseReferenceXObject = mbUseReferenceXObject;

            // all context data set, time to create the printing device
            vcl::PDFWriter aPDFWriter( aContext, xEnc );
            OutputDevice*  pOut = aPDFWriter.GetReferenceDevice();

            DBG_ASSERT( pOut, "PDFExport::Export: no reference device" );
            xDevice->SetOutputDevice(pOut);

            if( mbAddStream )
            {
                // export stream
                // get mimetype
                OUString aSrcMimetype = getMimetypeForDocument( mxContext, mxSrcDoc );
                OUString aExt;
                if (aSrcMimetype == "application/vnd.oasis.opendocument.text")
                    aExt = ".odt";
                else if (aSrcMimetype == "application/vnd.oasis.opendocument.presentation")
                    aExt = ".odp";
                else if (aSrcMimetype == "application/vnd.oasis.opendocument.spreadsheet")
                    aExt = ".ods";
                else if (aSrcMimetype == "application/vnd.oasis.opendocument.graphics")
                    aExt = ".odg";
                std::unique_ptr<vcl::PDFOutputStream> pStream(new PDFExportStreamDoc(mxSrcDoc, aPreparedPermissionPassword));
                aPDFWriter.AddAttachedFile("Original" + aExt, aSrcMimetype, u"Embedded original document of this PDF file", std::move(pStream));
            }

            if ( pOut )
            {
                DBG_ASSERT( pOut->GetExtOutDevData() == nullptr, "PDFExport: ExtOutDevData already set!!!" );
                vcl::PDFExtOutDevData aPDFExtOutDevData( *pOut );
                pOut->SetExtOutDevData( &aPDFExtOutDevData );
                aPDFExtOutDevData.SetIsExportNotes( mbExportNotes );
                aPDFExtOutDevData.SetIsExportNotesInMargin( mbExportNotesInMargin );
                aPDFExtOutDevData.SetIsExportTaggedPDF( mbUseTaggedPDF );
                aPDFExtOutDevData.SetIsExportTransitionEffects( mbUseTransitionEffects );
                aPDFExtOutDevData.SetIsExportFormFields( mbExportFormFields );
                aPDFExtOutDevData.SetIsExportBookmarks( mbExportBookmarks );
                aPDFExtOutDevData.SetIsExportHiddenSlides( mbExportHiddenSlides );
                aPDFExtOutDevData.SetIsSinglePageSheets( mbSinglePageSheets );
                aPDFExtOutDevData.SetIsLosslessCompression( mbUseLosslessCompression );
                aPDFExtOutDevData.SetCompressionQuality( mnQuality );
                aPDFExtOutDevData.SetIsReduceImageResolution( mbReduceImageResolution );
                aPDFExtOutDevData.SetIsExportNamedDestinations( mbExportBmkToDest );

                Sequence< PropertyValue > aRenderOptions{
                    comphelper::makePropertyValue("RenderDevice", uno::Reference<awt::XDevice>(xDevice)),
                    comphelper::makePropertyValue("ExportNotesPages", false),
                    comphelper::makePropertyValue("IsFirstPage", true),
                    comphelper::makePropertyValue("IsLastPage", false),
                    comphelper::makePropertyValue("IsSkipEmptyPages", mbSkipEmptyPages),
                    comphelper::makePropertyValue("PageRange", aPageRange),
                    comphelper::makePropertyValue("ExportPlaceholders", mbExportPlaceholders),
                    comphelper::makePropertyValue("SinglePageSheets", mbSinglePageSheets),
                    comphelper::makePropertyValue("ExportNotesInMargin", mbExportNotesInMargin)
                };
                Any& rExportNotesValue = aRenderOptions.getArray()[ 1 ].Value;

                if( !aPageRange.isEmpty() || !aSelection.hasValue() )
                {
                    aSelection = Any();
                    aSelection <<= mxSrcDoc;
                }
                bool bExportNotesPages = false;
                bool bReChangeToNormalView = false;
                static const OUStringLiteral sShowOnlineLayout( u"ShowOnlineLayout" );
                bool bReHideWhitespace = false;
                static const OUStringLiteral sHideWhitespace(u"HideWhitespace");
                uno::Reference< beans::XPropertySet > xViewProperties;

                if ( aCreator == "Writer" )
                {
                    // #i92835: if Writer is in web layout mode this has to be switched to normal view and back to web view in the end
                    try
                    {
                        Reference< view::XViewSettingsSupplier > xVSettingsSupplier( xModel->getCurrentController(), uno::UNO_QUERY_THROW );
                        xViewProperties = xVSettingsSupplier->getViewSettings();
                        xViewProperties->getPropertyValue( sShowOnlineLayout ) >>= bReChangeToNormalView;
                        if( bReChangeToNormalView )
                        {
                            xViewProperties->setPropertyValue( sShowOnlineLayout, uno::Any( false ) );
                        }

                        // Also, disable hide-whitespace during export.
                        xViewProperties->getPropertyValue(sHideWhitespace) >>= bReHideWhitespace;
                        if (bReHideWhitespace)
                        {
                            xViewProperties->setPropertyValue(sHideWhitespace, uno::Any(false));
                        }
                    }
                    catch( const uno::Exception& )
                    {
                    }

                }

                const sal_Int32 nPageCount = xRenderable->getRendererCount( aSelection, aRenderOptions );

                if ( mbExportNotesPages && aCreator == "Impress" )
                {
                    uno::Reference< drawing::XShapes > xShapes;     // do not allow to export notes when exporting a selection
                    if ( ! ( aSelection >>= xShapes ) )
                        bExportNotesPages = true;
                }
                const bool bExportPages = !bExportNotesPages || !mbExportOnlyNotesPages;

                if( aPageRange.isEmpty() || mbSinglePageSheets)
                {
                    aPageRange = OUString::number( 1 ) + "-" + OUString::number(nPageCount );
                }
                StringRangeEnumerator aRangeEnum( aPageRange, 0, nPageCount-1 );

                if ( mxStatusIndicator.is() )
                {
                    sal_Int32 nTotalPageCount = aRangeEnum.size();
                    if ( bExportPages && bExportNotesPages )
                        nTotalPageCount *= 2;
                    mxStatusIndicator->start(FilterResId(PDF_PROGRESS_BAR), nTotalPageCount);
                }

                bRet = nPageCount > 0;

                if ( bRet && bExportPages )
                    bRet = ExportSelection( aPDFWriter, xRenderable, aSelection, aRangeEnum, aRenderOptions, nPageCount );

                if ( bRet && bExportNotesPages )
                {
                    rExportNotesValue <<= true;
                    bRet = ExportSelection( aPDFWriter, xRenderable, aSelection, aRangeEnum, aRenderOptions, nPageCount );
                }
                if ( mxStatusIndicator.is() )
                    mxStatusIndicator->end();

                // if during the export the doc locale was set copy it to PDF writer
                const css::lang::Locale& rLoc( aPDFExtOutDevData.GetDocumentLocale() );
                if( !rLoc.Language.isEmpty() )
                    aPDFWriter.SetDocumentLocale( rLoc );

                if( bRet )
                {
                    aPDFExtOutDevData.PlayGlobalActions( aPDFWriter );
                    bRet = aPDFWriter.Emit();
                    aErrors = aPDFWriter.GetErrors();
                }
                pOut->SetExtOutDevData( nullptr );
                if( bReChangeToNormalView )
                {
                    try
                    {
                        xViewProperties->setPropertyValue( sShowOnlineLayout, uno::Any( true ) );
                    }
                    catch( const uno::Exception& )
                    {
                    }
                }
                if( bReHideWhitespace )
                {
                    try
                    {
                        xViewProperties->setPropertyValue( sHideWhitespace, uno::Any( true ) );
                    }
                    catch( const uno::Exception& )
                    {
                    }
                }
            }
        }
    }

    // show eventual errors during export
    showErrors( aErrors );

    return bRet;
}


namespace
{

typedef comphelper::WeakComponentImplHelper< task::XInteractionRequest > PDFErrorRequestBase;

class PDFErrorRequest : public PDFErrorRequestBase
{
    task::PDFExportException maExc;
public:
    explicit PDFErrorRequest( task::PDFExportException aExc );

    // XInteractionRequest
    virtual uno::Any SAL_CALL getRequest() override;
    virtual uno::Sequence< uno::Reference< task::XInteractionContinuation > > SAL_CALL getContinuations() override;
};


PDFErrorRequest::PDFErrorRequest( task::PDFExportException aExc ) :
    maExc(std::move( aExc ))
{
}


uno::Any SAL_CALL PDFErrorRequest::getRequest()
{
    std::unique_lock guard( m_aMutex );

    uno::Any aRet;
    aRet <<= maExc;
    return aRet;
}


uno::Sequence< uno::Reference< task::XInteractionContinuation > > SAL_CALL PDFErrorRequest::getContinuations()
{
    return uno::Sequence< uno::Reference< task::XInteractionContinuation > >();
}

} // end anonymous namespace


void PDFExport::showErrors( const std::set< vcl::PDFWriter::ErrorCode >& rErrors )
{
    if( ! rErrors.empty() && mxIH.is() )
    {
        task::PDFExportException aExc;
        aExc.ErrorCodes = comphelper::containerToSequence<sal_Int32>( rErrors );
        Reference< task::XInteractionRequest > xReq( new PDFErrorRequest( std::move(aExc) ) );
        mxIH->handle( xReq );
    }
}


void PDFExport::ImplExportPage( vcl::PDFWriter& rWriter, vcl::PDFExtOutDevData& rPDFExtOutDevData, const GDIMetaFile& rMtf )
{
    //Rectangle(Point, Size) creates a rectangle off by 1, use Rectangle(long, long, long, long) instead
    basegfx::B2DPolygon aSize(tools::Polygon(tools::Rectangle(0, 0, rMtf.GetPrefSize().Width(), rMtf.GetPrefSize().Height())).getB2DPolygon());
    basegfx::B2DPolygon aSizePDF(OutputDevice::LogicToLogic(aSize, rMtf.GetPrefMapMode(), MapMode(MapUnit::MapPoint)));
    basegfx::B2DRange aRangePDF(aSizePDF.getB2DRange());
    tools::Rectangle       aPageRect( Point(), rMtf.GetPrefSize() );

    rWriter.NewPage( aRangePDF.getWidth(), aRangePDF.getHeight() );
    rWriter.SetMapMode( rMtf.GetPrefMapMode() );

    vcl::PDFWriter::PlayMetafileContext aCtx;
    GDIMetaFile aMtf;
    if( mbRemoveTransparencies )
    {
        aCtx.m_bTransparenciesWereRemoved = rWriter.GetReferenceDevice()->
            RemoveTransparenciesFromMetaFile( rMtf, aMtf, mnMaxImageResolution, mnMaxImageResolution,
                                              false, true, mbReduceImageResolution );
        // tdf#134736 if the metafile was replaced then rPDFExtOutDevData's PageSyncData mActions
        // all still point to MetaAction indexes in the original metafile that are now invalid.
        // Throw them all away in the absence of a way to reposition them to new positions of
        // their replacements.
        if (aCtx.m_bTransparenciesWereRemoved)
            rPDFExtOutDevData.ResetSyncData();
    }
    else
    {
        aMtf = rMtf;
    }
    aCtx.m_nMaxImageResolution      = mbReduceImageResolution ? mnMaxImageResolution : 0;
    aCtx.m_bOnlyLosslessCompression = mbUseLosslessCompression;
    aCtx.m_nJPEGQuality             = mnQuality;


    rWriter.SetClipRegion( basegfx::B2DPolyPolygon(
        basegfx::utils::createPolygonFromRect( vcl::unotools::b2DRectangleFromRectangle(aPageRect) ) ) );

    rWriter.PlayMetafile( aMtf, aCtx, &rPDFExtOutDevData );

    rPDFExtOutDevData.ResetSyncData();

    if (!msWatermark.isEmpty())
    {
        ImplWriteWatermark( rWriter, Size(aRangePDF.getWidth(), aRangePDF.getHeight()) );
    }
    else if (!msTiledWatermark.isEmpty())
    {
        ImplWriteTiledWatermark( rWriter, Size(aRangePDF.getWidth(), aRangePDF.getHeight()) );
    }
}


void PDFExport::ImplWriteWatermark( vcl::PDFWriter& rWriter, const Size& rPageSize )
{
    vcl::Font aFont( maWatermarkFontName, Size( 0, moWatermarkFontHeight ? *moWatermarkFontHeight : 3*rPageSize.Height()/4 ) );
    aFont.SetItalic( ITALIC_NONE );
    aFont.SetWidthType( WIDTH_NORMAL );
    aFont.SetWeight( WEIGHT_NORMAL );
    aFont.SetAlignment( ALIGN_BOTTOM );
    tools::Long nTextWidth = rPageSize.Width();
    if( rPageSize.Width() < rPageSize.Height() )
    {
        nTextWidth = rPageSize.Height();
        aFont.SetOrientation( 2700_deg10 );
    }

    if (moWatermarkRotateAngle)
    {
        aFont.SetOrientation(*moWatermarkRotateAngle);
        if (rPageSize.Width() < rPageSize.Height())
        {
            // Set text width based on the shorter side, so rotation can't push text outside the
            // page boundaries.
            nTextWidth = rPageSize.Width();
        }
    }

    // adjust font height for text to fit
    OutputDevice* pDev = rWriter.GetReferenceDevice();
    pDev->Push();
    pDev->SetFont( aFont );
    pDev->SetMapMode( MapMode( MapUnit::MapPoint ) );
    int w = 0;
    if (moWatermarkFontHeight)
    {
        w = pDev->GetTextWidth(msWatermark);
    }
    else
    {
        while( ( w = pDev->GetTextWidth( msWatermark ) ) > nTextWidth )
        {
            if (w == 0)
                break;
            tools::Long nNewHeight = aFont.GetFontHeight() * nTextWidth / w;
            if( nNewHeight == aFont.GetFontHeight() )
            {
                nNewHeight--;
                if( nNewHeight <= 0 )
                    break;
            }
            aFont.SetFontHeight( nNewHeight );
            pDev->SetFont( aFont );
        }
    }
    tools::Long nTextHeight = pDev->GetTextHeight();
    // leave some maneuvering room for rounding issues, also
    // some fonts go a little outside ascent/descent
    nTextHeight += nTextHeight/20;
    pDev->Pop();

    rWriter.Push();
    rWriter.SetMapMode( MapMode( MapUnit::MapPoint ) );
    rWriter.SetFont( aFont );
    rWriter.SetTextColor(maWatermarkColor);
    Point aTextPoint;
    tools::Rectangle aTextRect;
    if( rPageSize.Width() > rPageSize.Height() )
    {
        aTextPoint = Point( (rPageSize.Width()-w)/2,
                            rPageSize.Height()-(rPageSize.Height()-nTextHeight)/2 );
        aTextRect = tools::Rectangle( Point( (rPageSize.Width()-w)/2,
                                      (rPageSize.Height()-nTextHeight)/2 ),
                               Size( w, nTextHeight ) );
    }
    else
    {
        aTextPoint = Point( (rPageSize.Width()-nTextHeight)/2,
                            (rPageSize.Height()-w)/2 );
        aTextRect = tools::Rectangle( aTextPoint, Size( nTextHeight, w ) );
    }

    if (moWatermarkRotateAngle)
    {
        // First set the text's starting point to the center of the page.
        tools::Rectangle aPageRectangle(Point(0, 0), rPageSize);
        aTextPoint = aPageRectangle.Center();
        // Then adjust it so that the text remains centered, based on the rotation angle.
        basegfx::B2DPolygon aTextPolygon
            = basegfx::utils::createPolygonFromRect(basegfx::B2DRectangle(0, -nTextHeight, w, 0));
        basegfx::B2DHomMatrix aMatrix;
        aMatrix.rotate(-1 * toRadians(*moWatermarkRotateAngle));
        aTextPolygon.transform(aMatrix);
        basegfx::B2DPoint aPolygonCenter = aTextPolygon.getB2DRange().getCenter();
        aTextPoint.AdjustX(-aPolygonCenter.getX());
        aTextPoint.AdjustY(-aPolygonCenter.getY());

        aTextRect = aPageRectangle;
    }

    rWriter.SetClipRegion();
    rWriter.BeginTransparencyGroup();
    rWriter.DrawText( aTextPoint, msWatermark );
    rWriter.EndTransparencyGroup( aTextRect, 50 );
    rWriter.Pop();
}

void PDFExport::ImplWriteTiledWatermark( vcl::PDFWriter& rWriter, const Size& rPageSize )
{
    OUString watermark = msTiledWatermark;
    // Maximum number of characters in one line.
    // it is set to 21 to make it look like tiled watermarks as online in secure view
    const int lineLength = 21;
    vcl::Font aFont( "Liberation Sans", Size( 0, 40 ) );
    aFont.SetItalic( ITALIC_NONE );
    aFont.SetWidthType( WIDTH_NORMAL );
    aFont.SetWeight( WEIGHT_NORMAL );
    aFont.SetAlignment( ALIGN_BOTTOM );
    aFont.SetFontHeight(40);
    aFont.SetOrientation(450_deg10);

    OutputDevice* pDev = rWriter.GetReferenceDevice();
    pDev->SetFont(aFont);
    pDev->Push();
    pDev->SetFont(aFont);
    pDev->SetMapMode( MapMode( MapUnit::MapPoint ) );
    int w = 0;
    int watermarkcount = ((rPageSize.Width()) / 200)+1;
    tools::Long nTextWidth = rPageSize.Width() / (watermarkcount*1.5);
    OUString oneLineText = watermark;

    if(watermark.getLength() > lineLength)
        oneLineText = watermark.copy(0, lineLength);

    while((w = pDev->GetTextWidth(oneLineText)) > nTextWidth)
    {
        if(w==0)
            break;

        tools::Long nNewHeight = aFont.GetFontHeight() * nTextWidth / w;
        aFont.SetFontHeight(nNewHeight);
        pDev->SetFont( aFont );
    }
    // maximum number of watermark count for the width
    if(watermarkcount > 8)
        watermarkcount = 8;

    pDev->Pop();

    rWriter.Push();
    rWriter.SetMapMode( MapMode( MapUnit::MapPoint ) );
    rWriter.SetFont(aFont);
    rWriter.SetTextColor( Color(19,20,22) );
    // center watermarks horizontally
    Point aTextPoint( (rPageSize.Width()/2) - (((nTextWidth*watermarkcount)+(watermarkcount-1)*nTextWidth)/2),
                      pDev->GetTextHeight());

    for( int i = 0; i < watermarkcount; i ++)
    {
        while(aTextPoint.getY()+pDev->GetTextHeight()*3 <= rPageSize.Height())
        {
            tools::Rectangle aTextRect(aTextPoint, Size(nTextWidth*2,pDev->GetTextHeight()*4));

            pDev->Push();
            rWriter.SetClipRegion();
            rWriter.BeginTransparencyGroup();
            rWriter.SetTextColor( Color(19,20,22) );
            rWriter.DrawText(aTextRect, watermark, DrawTextFlags::MultiLine|DrawTextFlags::Center|DrawTextFlags::VCenter|DrawTextFlags::WordBreak|DrawTextFlags::Bottom);
            rWriter.EndTransparencyGroup( aTextRect, 50 );
            pDev->Pop();

            pDev->Push();
            rWriter.SetClipRegion();
            rWriter.BeginTransparencyGroup();
            rWriter.SetTextColor( Color(236,235,233) );
            rWriter.DrawText(aTextRect, watermark, DrawTextFlags::MultiLine|DrawTextFlags::Center|DrawTextFlags::VCenter|DrawTextFlags::WordBreak|DrawTextFlags::Bottom);
            rWriter.EndTransparencyGroup( aTextRect, 50 );
            pDev->Pop();

            aTextPoint.Move(0, pDev->GetTextHeight()*3);
        }
        aTextPoint=Point( aTextPoint.getX(), pDev->GetTextHeight() );
        aTextPoint.Move( nTextWidth*1.5, 0 );
    }

    rWriter.Pop();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
