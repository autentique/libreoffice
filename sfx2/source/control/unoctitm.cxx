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

#include <config_java.h>

#include <tools/debug.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <svl/itempool.hxx>
#include <svl/itemset.hxx>
#include <svl/stritem.hxx>
#include <svl/visitem.hxx>
#include <svtools/javacontext.hxx>
#include <svtools/javainteractionhandler.hxx>
#include <tools/urlobj.hxx>
#include <com/sun/star/awt/FontDescriptor.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/status/FontHeight.hpp>
#include <com/sun/star/frame/status/ItemStatus.hpp>
#include <com/sun/star/frame/status/ItemState.hpp>
#include <com/sun/star/frame/status/Template.hpp>
#include <com/sun/star/frame/DispatchResultState.hpp>
#include <com/sun/star/frame/status/Visibility.hpp>
#include <comphelper/processfactory.hxx>
#include <uno/current_context.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/uitest/logger.hxx>
#include <boost/property_tree/json_parser.hpp>
#include <tools/json_writer.hxx>

#include <sfx2/app.hxx>
#include <unoctitm.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/frame.hxx>
#include <sfx2/ctrlitem.hxx>
#include <sfx2/sfxuno.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/request.hxx>
#include <sfx2/msg.hxx>
#include <sfx2/viewsh.hxx>
#include <slotserv.hxx>
#include <rtl/ustring.hxx>
#include <sfx2/lokhelper.hxx>

#include <memory>
#include <string_view>

#include <sal/log.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <comphelper/lok.hxx>
#include <comphelper/servicehelper.hxx>

#include <desktop/crashreport.hxx>
#include <vcl/threadex.hxx>
#include <unotools/mediadescriptor.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::util;

namespace {

enum URLTypeId
{
    URLType_BOOL,
    URLType_BYTE,
    URLType_SHORT,
    URLType_LONG,
    URLType_HYPER,
    URLType_STRING,
    URLType_FLOAT,
    URLType_DOUBLE,
    URLType_COUNT
};

}

const char* const URLTypeNames[URLType_COUNT] =
{
    "bool",
    "byte",
    "short",
    "long",
    "hyper",
    "string",
    "float",
    "double"
};

static void InterceptLOKStateChangeEvent( sal_uInt16 nSID, SfxViewFrame* pViewFrame, const css::frame::FeatureStateEvent& aEvent, const SfxPoolItem* pState );

void SfxStatusDispatcher::ReleaseAll()
{
    css::lang::EventObject aObject;
    aObject.Source = getXWeak();
    std::unique_lock aGuard(maMutex);
    maListeners.disposeAndClear( aGuard, aObject );
}

void SfxStatusDispatcher::sendStatusChanged(const OUString& rURL, const css::frame::FeatureStateEvent& rEvent)
{
    std::unique_lock aGuard(maMutex);
    ::comphelper::OInterfaceContainerHelper4<css::frame::XStatusListener>* pContnr = maListeners.getContainer(aGuard, rURL);
    if (!pContnr)
        return;
    pContnr->forEach(aGuard,
        [&rEvent](const css::uno::Reference<css::frame::XStatusListener>& xListener)
        {
            xListener->statusChanged(rEvent);
        }
    );
}

void SAL_CALL SfxStatusDispatcher::dispatch( const css::util::URL&, const css::uno::Sequence< css::beans::PropertyValue >& )
{
}

void SAL_CALL SfxStatusDispatcher::dispatchWithNotification(
    const css::util::URL&,
    const css::uno::Sequence< css::beans::PropertyValue >&,
    const css::uno::Reference< css::frame::XDispatchResultListener >& )
{
}

SfxStatusDispatcher::SfxStatusDispatcher()
{
}

void SAL_CALL SfxStatusDispatcher::addStatusListener(const css::uno::Reference< css::frame::XStatusListener > & aListener, const css::util::URL& aURL)
{
    {
        std::unique_lock aGuard(maMutex);
        maListeners.addInterface( aGuard, aURL.Complete, aListener );
    }
    if ( aURL.Complete == ".uno:LifeTime" )
    {
        css::frame::FeatureStateEvent aEvent;
        aEvent.FeatureURL = aURL;
        aEvent.Source = static_cast<css::frame::XDispatch*>(this);
        aEvent.IsEnabled = true;
        aEvent.Requery = false;
        aListener->statusChanged( aEvent );
    }
}

void SAL_CALL SfxStatusDispatcher::removeStatusListener( const css::uno::Reference< css::frame::XStatusListener > & aListener, const css::util::URL& aURL )
{
    std::unique_lock aGuard(maMutex);
    maListeners.removeInterface( aGuard, aURL.Complete, aListener );
}


SfxOfficeDispatch::SfxOfficeDispatch( SfxBindings& rBindings, SfxDispatcher* pDispat, const SfxSlot* pSlot, const css::util::URL& rURL )
    : pImpl( new SfxDispatchController_Impl( this, &rBindings, pDispat, pSlot, rURL ))
{
    // pImpl is an adapter that shows a css::frame::XDispatch-Interface to the outside and uses a SfxControllerItem to monitor a state

}

SfxOfficeDispatch::SfxOfficeDispatch( SfxDispatcher* pDispat, const SfxSlot* pSlot, const css::util::URL& rURL )
    : pImpl( new SfxDispatchController_Impl( this, nullptr, pDispat, pSlot, rURL ))
{
    // pImpl is an adapter that shows a css::frame::XDispatch-Interface to the outside and uses a SfxControllerItem to monitor a state
}

SfxOfficeDispatch::~SfxOfficeDispatch()
{
    if ( pImpl )
    {
        // when dispatch object is released, destroy its connection to this object and destroy it
        pImpl->UnBindController();

        // Ensure that SfxDispatchController_Impl is deleted while the solar mutex is locked, since
        // that derives from SfxListener.
        SolarMutexGuard aGuard;
        pImpl.reset();
    }
}

#if HAVE_FEATURE_JAVA
// The JavaContext contains an interaction handler which is used when
// the creation of a Java Virtual Machine fails. There shall only be one
// user notification (message box) even if the same error (interaction)
// reoccurs. The effect is, that if a user selects a menu entry than they
// may get only one notification that a JRE is not selected.
// This function checks if a JavaContext is already available (typically
// created by Desktop::Main() in app.cxx), and creates new one if not.
namespace {
std::unique_ptr< css::uno::ContextLayer > EnsureJavaContext()
{
    css::uno::Reference< css::uno::XCurrentContext > xContext(css::uno::getCurrentContext());
    if (xContext.is())
    {
        css::uno::Reference< css::task::XInteractionHandler > xHandler;
        xContext->getValueByName(JAVA_INTERACTION_HANDLER_NAME) >>= xHandler;
        if (xHandler.is())
            return nullptr; // No need to add new layer: JavaContext already present
    }
    return std::make_unique< css::uno::ContextLayer >(new svt::JavaContext(xContext));
}
}
#endif

void SAL_CALL SfxOfficeDispatch::dispatch( const css::util::URL& aURL, const css::uno::Sequence< css::beans::PropertyValue >& aArgs )
{
    // ControllerItem is the Impl class
    if ( pImpl )
    {
#if HAVE_FEATURE_JAVA
        std::unique_ptr< css::uno::ContextLayer > layer(EnsureJavaContext());
#endif
        utl::MediaDescriptor aDescriptor(aArgs);
        bool bOnMainThread = aDescriptor.getUnpackedValueOrDefault("OnMainThread", false);
        if (bOnMainThread)
        {
            // Make sure that we own the solar mutex, otherwise later
            // vcl::SolarThreadExecutor::execute() will release the solar mutex, even if it's owned by
            // another thread, leading to an std::abort() at the end.
            SolarMutexGuard aGuard;
            vcl::solarthread::syncExecute([this, &aURL, &aArgs]() {
                pImpl->dispatch(aURL, aArgs,
                                css::uno::Reference<css::frame::XDispatchResultListener>());
            });
        }
        else
        {
            pImpl->dispatch(aURL, aArgs,
                            css::uno::Reference<css::frame::XDispatchResultListener>());
        }
    }
}

void SAL_CALL SfxOfficeDispatch::dispatchWithNotification( const css::util::URL& aURL,
        const css::uno::Sequence< css::beans::PropertyValue >& aArgs,
        const css::uno::Reference< css::frame::XDispatchResultListener >& rListener )
{
    // ControllerItem is the Impl class
    if ( pImpl )
    {
#if HAVE_FEATURE_JAVA
        std::unique_ptr< css::uno::ContextLayer > layer(EnsureJavaContext());
#endif
        pImpl->dispatch( aURL, aArgs, rListener );
    }
}

void SAL_CALL SfxOfficeDispatch::addStatusListener(const css::uno::Reference< css::frame::XStatusListener > & aListener, const css::util::URL& aURL)
{
    {
        std::unique_lock aGuard(maMutex);
        maListeners.addInterface( aGuard, aURL.Complete, aListener );
    }
    if ( pImpl )
    {
        // ControllerItem is the Impl class
        pImpl->addStatusListener( aListener, aURL );
    }
}

SfxDispatcher* SfxOfficeDispatch::GetDispatcher_Impl()
{
    return pImpl->GetDispatcher();
}

sal_uInt16 SfxOfficeDispatch::GetId() const
{
    return pImpl ? pImpl->GetId() : 0;
}

void SfxOfficeDispatch::SetFrame(const css::uno::Reference< css::frame::XFrame >& xFrame)
{
    if ( pImpl )
        pImpl->SetFrame( xFrame );
}

void SfxOfficeDispatch::SetMasterUnoCommand( bool bSet )
{
    if ( pImpl )
        pImpl->setMasterSlaveCommand( bSet );
}

// Determine if URL contains a master/slave command which must be handled a little bit different
bool SfxOfficeDispatch::IsMasterUnoCommand( const css::util::URL& aURL )
{
    return aURL.Protocol == ".uno:" && ( aURL.Path.indexOf( '.' ) > 0 );
}

OUString SfxOfficeDispatch::GetMasterUnoCommand( const css::util::URL& aURL )
{
    OUString aMasterCommand;
    if ( IsMasterUnoCommand( aURL ))
    {
        sal_Int32 nIndex = aURL.Path.indexOf( '.' );
        if ( nIndex > 0 )
            aMasterCommand = aURL.Path.copy( 0, nIndex );
    }

    return aMasterCommand;
}

SfxDispatchController_Impl::SfxDispatchController_Impl(
    SfxOfficeDispatch*                 pDisp,
    SfxBindings*                       pBind,
    SfxDispatcher*                     pDispat,
    const SfxSlot*                     pSlot,
    css::util::URL               aURL )
    : aDispatchURL(std::move( aURL ))
    , pDispatcher( pDispat )
    , pBindings( pBind )
    , pLastState( nullptr )
    , pDispatch( pDisp )
    , bMasterSlave( false )
    , bVisible( true )
{
    if ( aDispatchURL.Protocol == "slot:" && !pSlot->pUnoName.isEmpty() )
    {
        aDispatchURL.Complete = pSlot->GetCommand();
        Reference< XURLTransformer > xTrans( URLTransformer::create( ::comphelper::getProcessComponentContext() ) );
        xTrans->parseStrict( aDispatchURL );
    }

    sal_uInt16 nSlot = pSlot->GetSlotId();
    SetId( nSlot );
    if ( pBindings )
    {
        // Bind immediately to enable the cache to recycle dispatches when asked for the same command
        // a command in "slot" or in ".uno" notation must be treated as identical commands!
        pBindings->ENTERREGISTRATIONS();
        BindInternal_Impl( nSlot, pBindings );
        pBindings->LEAVEREGISTRATIONS();
    }
    assert(pDispatcher);
    assert(SfxApplication::Get()->GetAppDispatcher_Impl() == pDispatcher
        || pDispatcher->GetFrame() != nullptr);
    if (pDispatcher->GetFrame())
    {
        StartListening(*pDispatcher->GetFrame());
    }
    else
    {
        StartListening(*SfxApplication::Get());
    }
}

void SfxDispatchController_Impl::Notify(SfxBroadcaster& rBC, SfxHint const& rHint)
{
    if (rHint.GetId() == SfxHintId::Dying)
    {   // both pBindings and pDispatcher are dead if SfxViewFrame is dead
        pBindings = nullptr;
        pDispatcher = nullptr;
        EndListening(rBC);
    }
}

SfxDispatchController_Impl::~SfxDispatchController_Impl()
{
    if ( pLastState && !IsInvalidItem( pLastState ) )
        delete pLastState;

    if ( pDispatch )
    {
        // disconnect
        pDispatch->pImpl = nullptr;

        // force all listeners to release the dispatch object
        pDispatch->ReleaseAll();
    }
}

void SfxDispatchController_Impl::SetFrame(const css::uno::Reference< css::frame::XFrame >& _xFrame)
{
    xFrame = _xFrame;
}

void SfxDispatchController_Impl::setMasterSlaveCommand( bool bSet )
{
    bMasterSlave = bSet;
}

void SfxDispatchController_Impl::UnBindController()
{
    pDispatch = nullptr;
    if ( IsBound() )
    {
        GetBindings().ENTERREGISTRATIONS();
        SfxControllerItem::UnBind();
        GetBindings().LEAVEREGISTRATIONS();
    }
}

void SfxDispatchController_Impl::addParametersToArgs( const css::util::URL& aURL, css::uno::Sequence< css::beans::PropertyValue >& rArgs )
{
    // Extract the parameter from the URL and put them into the property value sequence
    sal_Int32 nQueryIndex = aURL.Complete.indexOf( '?' );
    if ( nQueryIndex <= 0 )
        return;

    OUString aParamString( aURL.Complete.copy( nQueryIndex+1 ));
    sal_Int32 nIndex = 0;
    do
    {
        OUString aToken = aParamString.getToken( 0, '&', nIndex );

        sal_Int32 nParmIndex = 0;
        OUString aParamType;
        OUString aParamName = aToken.getToken( 0, '=', nParmIndex );
        OUString aValue     = aToken.getToken( 0, '=', nParmIndex );

        if ( !aParamName.isEmpty() )
        {
            nParmIndex = 0;
            aToken = aParamName;
            aParamName = aToken.getToken( 0, ':', nParmIndex );
            aParamType = aToken.getToken( 0, ':', nParmIndex );
        }

        sal_Int32 nLen = rArgs.getLength();
        rArgs.realloc( nLen+1 );
        auto pArgs = rArgs.getArray();
        pArgs[nLen].Name = aParamName;

        if ( aParamType.isEmpty() )
        {
            // Default: LONG
            pArgs[nLen].Value <<= aValue.toInt32();
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_BOOL], 4 ))
        {
            // sal_Bool support
            pArgs[nLen].Value <<= aValue.toBoolean();
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_BYTE], 4 ))
        {
            // sal_uInt8 support
            pArgs[nLen].Value <<= sal_Int8( aValue.toInt32() );
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_LONG], 4 ))
        {
            // LONG support
            pArgs[nLen].Value <<= aValue.toInt32();
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_SHORT], 5 ))
        {
            // SHORT support
            pArgs[nLen].Value <<= sal_Int16( aValue.toInt32() );
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_HYPER], 5 ))
        {
            // HYPER support
            pArgs[nLen].Value <<= aValue.toInt64();
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_FLOAT], 5 ))
        {
            // FLOAT support
            pArgs[nLen].Value <<= aValue.toFloat();
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_STRING], 6 ))
        {
            // STRING support
            pArgs[nLen].Value <<= INetURLObject::decode( aValue, INetURLObject::DecodeMechanism::WithCharset );
        }
        else if ( aParamType.equalsAsciiL( URLTypeNames[URLType_DOUBLE], 6))
        {
            // DOUBLE support
            pArgs[nLen].Value <<= aValue.toDouble();
        }
    }
    while ( nIndex >= 0 );
}

MapUnit SfxDispatchController_Impl::GetCoreMetric( SfxItemPool const & rPool, sal_uInt16 nSlotId )
{
    sal_uInt16 nWhich = rPool.GetWhich( nSlotId );
    return rPool.GetMetric( nWhich );
}

OUString SfxDispatchController_Impl::getSlaveCommand( const css::util::URL& rURL )
{
    OUString   aSlaveCommand;
    sal_Int32       nIndex = rURL.Path.indexOf( '.' );
    if (( nIndex > 0 ) && ( nIndex < rURL.Path.getLength() ))
        aSlaveCommand = rURL.Path.copy( nIndex+1 );
    return aSlaveCommand;
}

namespace {

void collectUIInformation(const util::URL& rURL, const css::uno::Sequence< css::beans::PropertyValue >& rArgs)
{
    static const char* pFile = std::getenv("LO_COLLECT_UIINFO");
    if (!pFile)
        return;

    UITestLogger::getInstance().logCommand(
        Concat2View("Send UNO Command (\"" + rURL.Complete + "\") "), rArgs);
}

}

void SfxDispatchController_Impl::dispatch( const css::util::URL& aURL,
        const css::uno::Sequence< css::beans::PropertyValue >& aArgs,
        const css::uno::Reference< css::frame::XDispatchResultListener >& rListener )
{
    if ( aURL.Protocol == ".uno:")
    {
        CrashReporter::logUnoCommand(aURL.Path);
    }
    collectUIInformation(aURL, aArgs);

    SolarMutexGuard aGuard;

    if (comphelper::LibreOfficeKit::isActive() &&
        SfxViewShell::Current()->isBlockedCommand(aURL.Complete))
    {
        tools::JsonWriter aTree;
        aTree.put("code", "");
        aTree.put("kind", "BlockedCommand");
        aTree.put("cmd", aURL.Complete);
        aTree.put("message", "Blocked feature");
        aTree.put("viewID", SfxViewShell::Current()->GetViewShellId().get());

        SfxViewShell::Current()->libreOfficeKitViewCallback(LOK_COMMAND_BLOCKED, aTree.finishAndGetAsOString());
        return;
    }

    if (
        !(pDispatch &&
        (
         (aURL.Protocol == ".uno:" && aURL.Path == aDispatchURL.Path) ||
         (aURL.Protocol == "slot:" && aURL.Path.toInt32() == GetId())
        ))
       )
        return;

    if ( !pDispatcher && pBindings )
        pDispatcher = GetBindings().GetDispatcher_Impl();

    css::uno::Sequence< css::beans::PropertyValue > lNewArgs;
    sal_Int32 nCount = aArgs.getLength();

    // Support for URL based arguments
    INetURLObject aURLObj( aURL.Complete );
    if ( aURLObj.HasParam() )
        addParametersToArgs( aURL, lNewArgs );

    // Try to find call mode and frame name inside given arguments...
    SfxCallMode nCall = SfxCallMode::RECORD;
    sal_Int32   nMarkArg = -1;

    // Filter arguments which shouldn't be part of the sequence property value
    sal_uInt16  nModifier(0);
    std::vector< css::beans::PropertyValue > aAddArgs;
    for( sal_Int32 n=0; n<nCount; n++ )
    {
        const css::beans::PropertyValue& rProp = aArgs[n];
        if( rProp.Name == "SynchronMode" )
        {
            bool    bTemp;
            if( rProp.Value >>= bTemp )
                nCall = bTemp ? SfxCallMode::SYNCHRON : SfxCallMode::ASYNCHRON;
        }
        else if( rProp.Name == "Bookmark" )
        {
            nMarkArg = n;
            aAddArgs.push_back( aArgs[n] );
        }
        else if( rProp.Name == "KeyModifier" )
            rProp.Value >>= nModifier;
        else
            aAddArgs.push_back( aArgs[n] );
    }

    // Add needed arguments to sequence property value
    sal_uInt32 nAddArgs = aAddArgs.size();
    if ( nAddArgs > 0 )
    {
        sal_uInt32 nIndex( lNewArgs.getLength() );

        lNewArgs.realloc( nIndex + nAddArgs );
        std::copy(aAddArgs.begin(), aAddArgs.end(), std::next(lNewArgs.getArray(), nIndex));
    }

    // Overwrite possible detected synchron argument, if real listener exists (currently no other way)
    if ( rListener.is() )
        nCall = SfxCallMode::SYNCHRON;

    if( GetId() == SID_JUMPTOMARK && nMarkArg == - 1 )
    {
        // we offer dispatches for SID_JUMPTOMARK if the URL points to a bookmark inside the document
        // so we must retrieve this as an argument from the parsed URL
        lNewArgs.realloc( lNewArgs.getLength()+1 );
        auto& el = lNewArgs.getArray()[lNewArgs.getLength()-1];
        el.Name = "Bookmark";
        el.Value <<= aURL.Mark;
    }

    css::uno::Reference< css::frame::XFrame > xFrameRef(xFrame.get(), css::uno::UNO_QUERY);
    if (! xFrameRef.is() && pDispatcher)
    {
        SfxViewFrame* pViewFrame = pDispatcher->GetFrame();
        if (pViewFrame)
            xFrameRef = pViewFrame->GetFrame().GetFrameInterface();
    }

    bool bSuccess = false;
    const SfxPoolItem* pItem = nullptr;
    MapUnit eMapUnit( MapUnit::Map100thMM );

    // Extra scope so that aInternalSet is destroyed before
    // rListener->dispatchFinished potentially calls
    // framework::Desktop::terminate -> SfxApplication::Deinitialize ->
    // ~CntItemPool:
    if (pDispatcher)
    {
        SfxAllItemSet aInternalSet( SfxGetpApp()->GetPool() );
        if (xFrameRef.is()) // an empty set is no problem ... but an empty frame reference can be a problem !
            aInternalSet.Put( SfxUnoFrameItem( SID_FILLFRAME, xFrameRef ) );

        SfxShell* pShell( nullptr );
        // #i102619# Retrieve metric from shell before execution - the shell could be destroyed after execution
        if ( pDispatcher->GetBindings() )
        {
            if ( !pDispatcher->IsLocked() )
            {
                const SfxSlot *pSlot = nullptr;
                if ( pDispatcher->GetShellAndSlot_Impl( GetId(), &pShell, &pSlot, false, false ) )
                {
                    if ( bMasterSlave )
                    {
                        // Extract slave command and add argument to the args list. Master slot MUST
                        // have an argument that has the same name as the master slot and type is SfxStringItem.
                        sal_Int32 nIndex = lNewArgs.getLength();
                        lNewArgs.realloc( nIndex+1 );
                        auto plNewArgs = lNewArgs.getArray();
                        plNewArgs[nIndex].Name   = pSlot->pUnoName;
                        plNewArgs[nIndex].Value  <<= SfxDispatchController_Impl::getSlaveCommand( aDispatchURL );
                    }

                    eMapUnit = GetCoreMetric( pShell->GetPool(), GetId() );
                    std::optional<SfxAllItemSet> xSet(pShell->GetPool());
                    TransformParameters(GetId(), lNewArgs, *xSet, pSlot);
                    if (xSet->Count())
                    {
                        // execute with arguments - call directly
                        pItem = pDispatcher->Execute(GetId(), nCall, &*xSet, &aInternalSet, nModifier);
                        if ( pItem != nullptr )
                        {
                            if (const SfxBoolItem* pBoolItem = dynamic_cast<const SfxBoolItem*>(pItem))
                                bSuccess = pBoolItem->GetValue();
                            else if ( !pItem->IsVoidItem() )
                                bSuccess = true;  // all other types are true
                        }
                        // else bSuccess = false look to line 664 it is false
                    }
                    else
                    {
                        // Be sure to delete this before we send a dispatch
                        // request, which will destroy the current shell.
                        xSet.reset();

                        // execute using bindings, enables support for toggle/enum etc.
                        SfxRequest aReq( GetId(), nCall, pShell->GetPool() );
                        aReq.SetModifier( nModifier );
                        aReq.SetInternalArgs_Impl(aInternalSet);
                        pDispatcher->GetBindings()->Execute_Impl( aReq, pSlot, pShell );
                        pItem = aReq.GetReturnValue();
                        bSuccess = aReq.IsDone() || pItem != nullptr;
                    }
                }
                else
                    SAL_INFO("sfx.control", "MacroPlayer: Unknown slot dispatched!");
            }
        }
        else
        {
            eMapUnit = GetCoreMetric( SfxGetpApp()->GetPool(), GetId() );
            // AppDispatcher
            SfxAllItemSet aSet( SfxGetpApp()->GetPool() );
            TransformParameters( GetId(), lNewArgs, aSet );

            if ( aSet.Count() )
                pItem = pDispatcher->Execute(GetId(), nCall, &aSet, &aInternalSet, nModifier);
            else
                // SfxRequests take empty sets as argument sets, GetArgs() returning non-zero!
                pItem = pDispatcher->Execute(GetId(), nCall, nullptr, &aInternalSet, nModifier);

            // no bindings, no invalidate ( usually done in SfxDispatcher::Call_Impl()! )
            if (SfxApplication* pApp = SfxApplication::Get())
            {
                SfxDispatcher* pAppDispat = pApp->GetAppDispatcher_Impl();
                if ( pAppDispat )
                {
                    const SfxPoolItem* pState=nullptr;
                    SfxItemState eState = pDispatcher->QueryState( GetId(), pState );
                    StateChangedAtToolBoxControl( GetId(), eState, pState );
                }
            }

            bSuccess = (pItem != nullptr);
        }
    }

    if ( !rListener.is() )
        return;

    css::frame::DispatchResultEvent aEvent;
    if ( bSuccess )
        aEvent.State = css::frame::DispatchResultState::SUCCESS;
    else
        aEvent.State = css::frame::DispatchResultState::FAILURE;

    aEvent.Source = static_cast<css::frame::XDispatch*>(pDispatch);
    if ( bSuccess && pItem && !pItem->IsVoidItem() )
    {
        sal_uInt16 nSubId( 0 );
        if ( eMapUnit == MapUnit::MapTwip )
            nSubId |= CONVERT_TWIPS;
        pItem->QueryValue( aEvent.Result, static_cast<sal_uInt8>(nSubId) );
    }

    rListener->dispatchFinished( aEvent );
}

SfxDispatcher* SfxDispatchController_Impl::GetDispatcher()
{
    if ( !pDispatcher && pBindings )
        pDispatcher = GetBindings().GetDispatcher_Impl();
    return pDispatcher;
}

void SfxDispatchController_Impl::addStatusListener(const css::uno::Reference< css::frame::XStatusListener > & aListener, const css::util::URL& aURL)
{
    SolarMutexGuard aGuard;
    if ( !pDispatch )
        return;

    // Use alternative QueryState call to have a valid UNO representation of the state.
    css::uno::Any aState;
    if ( !pDispatcher && pBindings )
        pDispatcher = GetBindings().GetDispatcher_Impl();
    SfxItemState eState = pDispatcher ? pDispatcher->QueryState( GetId(), aState ) : SfxItemState::DONTCARE;

    if ( eState == SfxItemState::DONTCARE )
    {
        // Use special uno struct to transport don't care state
        css::frame::status::ItemStatus aItemStatus;
        aItemStatus.State = css::frame::status::ItemState::DONT_CARE;
        aState <<= aItemStatus;
    }

    css::frame::FeatureStateEvent  aEvent;
    aEvent.FeatureURL = aURL;
    aEvent.Source     = static_cast<css::frame::XDispatch*>(pDispatch);
    aEvent.Requery    = false;
    if ( bVisible )
    {
        aEvent.IsEnabled  = eState != SfxItemState::DISABLED;
        aEvent.State      = aState;
    }
    else
    {
        css::frame::status::Visibility aVisibilityStatus;
        aVisibilityStatus.bVisible = false;

        // MBA: we might decide to *not* disable "invisible" slots, but this would be
        // a change that needs to adjust at least the testtool
        aEvent.IsEnabled           = false;
        aEvent.State               <<= aVisibilityStatus;
    }

    aListener->statusChanged( aEvent );
}

void SfxDispatchController_Impl::sendStatusChanged(const OUString& rURL, const css::frame::FeatureStateEvent& rEvent)
{
    pDispatch->sendStatusChanged(rURL, rEvent);
}

void SfxDispatchController_Impl::StateChanged( sal_uInt16 nSID, SfxItemState eState, const SfxPoolItem* pState, SfxSlotServer const * pSlotServ )
{
    if ( !pDispatch )
        return;

    // Bindings instance notifies controller about a state change, listeners must be notified also
    // Don't cache visibility state changes as they are volatile. We need our real state to send it
    // to our controllers after visibility is set to true.
    bool bNotify = true;
    if ( pState && !IsInvalidItem( pState ) )
    {
        if ( auto pVisibilityItem = dynamic_cast< const SfxVisibilityItem *>( pState ) )
            bVisible = pVisibilityItem->GetValue();
        else
        {
            if (pLastState && !IsInvalidItem(pLastState))
            {
                bNotify = typeid(*pState) != typeid(*pLastState) || *pState != *pLastState;
                delete pLastState;
            }
            pLastState = !IsInvalidItem(pState) ? pState->Clone() : pState;
            bVisible = true;
        }
    }
    else
    {
        if ( pLastState && !IsInvalidItem( pLastState ) )
            delete pLastState;
        pLastState = pState;
    }

    if (!bNotify)
        return;

    css::uno::Any aState;
    if ( ( eState >= SfxItemState::DEFAULT ) && pState && !IsInvalidItem( pState ) && !pState->IsVoidItem() )
    {
        // Retrieve metric from pool to have correct sub ID when calling QueryValue
        sal_uInt16     nSubId( 0 );
        MapUnit eMapUnit( MapUnit::Map100thMM );

        // retrieve the core metric
        // it's enough to check the objectshell, the only shell that does not use the pool of the document
        // is SfxViewFrame, but it hasn't any metric parameters
        // TODO/LATER: what about the FormShell? Does it use any metric data?! Perhaps it should use the Pool of the document!
        if ( pSlotServ && pDispatcher )
        {
            if (SfxShell* pShell = pDispatcher->GetShell( pSlotServ->GetShellLevel() ))
                eMapUnit = GetCoreMetric( pShell->GetPool(), nSID );
        }

        if ( eMapUnit == MapUnit::MapTwip )
            nSubId |= CONVERT_TWIPS;

        pState->QueryValue( aState, static_cast<sal_uInt8>(nSubId) );
    }
    else if ( eState == SfxItemState::DONTCARE )
    {
        // Use special uno struct to transport don't care state
        css::frame::status::ItemStatus aItemStatus;
        aItemStatus.State = css::frame::status::ItemState::DONT_CARE;
        aState <<= aItemStatus;
    }

    css::frame::FeatureStateEvent aEvent;
    aEvent.FeatureURL = aDispatchURL;
    aEvent.Source = static_cast<css::frame::XDispatch*>(pDispatch);
    aEvent.IsEnabled = eState != SfxItemState::DISABLED;
    aEvent.Requery = false;
    aEvent.State = aState;

    if (pDispatcher && pDispatcher->GetFrame())
    {
        InterceptLOKStateChangeEvent(nSID, pDispatcher->GetFrame(), aEvent, pState);
    }

    const std::vector<OUString> aContainedTypes = pDispatch->getContainedTypes();
    for (const OUString& rName: aContainedTypes)
    {
        if (rName == aDispatchURL.Main || rName == aDispatchURL.Complete)
            sendStatusChanged(rName, aEvent);
    }
}

void SfxDispatchController_Impl::StateChangedAtToolBoxControl( sal_uInt16 nSID, SfxItemState eState, const SfxPoolItem* pState )
{
    StateChanged( nSID, eState, pState, nullptr );
}

static void InterceptLOKStateChangeEvent(sal_uInt16 nSID, SfxViewFrame* pViewFrame, const css::frame::FeatureStateEvent& aEvent, const SfxPoolItem* pState)
{
    if (!comphelper::LibreOfficeKit::isActive())
        return;

    OUStringBuffer aBuffer(aEvent.FeatureURL.Complete + "=");

    if (aEvent.FeatureURL.Path == "Bold" ||
        aEvent.FeatureURL.Path == "CenterPara" ||
        aEvent.FeatureURL.Path == "CharBackgroundExt" ||
        aEvent.FeatureURL.Path == "ControlCodes" ||
        aEvent.FeatureURL.Path == "DefaultBullet" ||
        aEvent.FeatureURL.Path == "DefaultNumbering" ||
        aEvent.FeatureURL.Path == "Italic" ||
        aEvent.FeatureURL.Path == "JustifyPara" ||
        aEvent.FeatureURL.Path == "LeftPara" ||
        aEvent.FeatureURL.Path == "OutlineFont" ||
        aEvent.FeatureURL.Path == "RightPara" ||
        aEvent.FeatureURL.Path == "Shadowed" ||
        aEvent.FeatureURL.Path == "SpellOnline" ||
        aEvent.FeatureURL.Path == "OnlineAutoFormat" ||
        aEvent.FeatureURL.Path == "SubScript" ||
        aEvent.FeatureURL.Path == "SuperScript" ||
        aEvent.FeatureURL.Path == "Strikeout" ||
        aEvent.FeatureURL.Path == "Underline" ||
        aEvent.FeatureURL.Path == "ModifiedStatus" ||
        aEvent.FeatureURL.Path == "TrackChanges" ||
        aEvent.FeatureURL.Path == "ShowTrackedChanges" ||
        aEvent.FeatureURL.Path == "NextTrackedChange" ||
        aEvent.FeatureURL.Path == "PreviousTrackedChange" ||
        aEvent.FeatureURL.Path == "AlignLeft" ||
        aEvent.FeatureURL.Path == "AlignHorizontalCenter" ||
        aEvent.FeatureURL.Path == "AlignRight" ||
        aEvent.FeatureURL.Path == "DocumentRepair" ||
        aEvent.FeatureURL.Path == "ObjectAlignLeft" ||
        aEvent.FeatureURL.Path == "ObjectAlignRight" ||
        aEvent.FeatureURL.Path == "AlignCenter" ||
        aEvent.FeatureURL.Path == "AlignUp" ||
        aEvent.FeatureURL.Path == "AlignMiddle" ||
        aEvent.FeatureURL.Path == "AlignDown" ||
        aEvent.FeatureURL.Path == "TraceChangeMode" ||
        aEvent.FeatureURL.Path == "FormatPaintbrush" ||
        aEvent.FeatureURL.Path == "FreezePanes" ||
        aEvent.FeatureURL.Path == "Sidebar" ||
        aEvent.FeatureURL.Path == "SheetRightToLeft" ||
        aEvent.FeatureURL.Path == "SpacePara1" ||
        aEvent.FeatureURL.Path == "SpacePara15" ||
        aEvent.FeatureURL.Path == "SpacePara2")
    {
        bool bTemp = false;
        aEvent.State >>= bTemp;
        aBuffer.append(bTemp);
    }
    else if (aEvent.FeatureURL.Path == "CharFontName")
    {
        css::awt::FontDescriptor aFontDesc;
        aEvent.State >>= aFontDesc;
        aBuffer.append(aFontDesc.Name);
    }
    else if (aEvent.FeatureURL.Path == "FontHeight")
    {
        css::frame::status::FontHeight aFontHeight;
        aEvent.State >>= aFontHeight;
        aBuffer.append(aFontHeight.Height);
    }
    else if (aEvent.FeatureURL.Path == "StyleApply")
    {
        css::frame::status::Template aTemplate;
        aEvent.State >>= aTemplate;
        aBuffer.append(aTemplate.StyleName);
    }
    else if (aEvent.FeatureURL.Path == "BackColor" ||
             aEvent.FeatureURL.Path == "BackgroundColor" ||
             aEvent.FeatureURL.Path == "CharBackColor" ||
             aEvent.FeatureURL.Path == "Color" ||
             aEvent.FeatureURL.Path == "FontColor" ||
             aEvent.FeatureURL.Path == "FrameLineColor" ||
             aEvent.FeatureURL.Path == "GlowColor")
    {
        sal_Int32 nColor = -1;
        aEvent.State >>= nColor;
        aBuffer.append(nColor);
    }
    else if (aEvent.FeatureURL.Path == "Undo" ||
             aEvent.FeatureURL.Path == "Redo")
    {
        const SfxUInt32Item* pUndoConflict = dynamic_cast< const SfxUInt32Item * >( pState );
        if ( pUndoConflict && pUndoConflict->GetValue() > 0 )
        {
            aBuffer.append("disabled");
        }
        else
        {
            aBuffer.append(aEvent.IsEnabled ? std::u16string_view(u"enabled") : std::u16string_view(u"disabled"));
        }
    }
    else if (aEvent.FeatureURL.Path == "Cut" ||
             aEvent.FeatureURL.Path == "Copy" ||
             aEvent.FeatureURL.Path == "Paste" ||
             aEvent.FeatureURL.Path == "SelectAll" ||
             aEvent.FeatureURL.Path == "InsertAnnotation" ||
             aEvent.FeatureURL.Path == "DeleteAnnotation" ||
             aEvent.FeatureURL.Path == "ResolveAnnotation" ||
             aEvent.FeatureURL.Path == "ResolveAnnotationThread" ||
             aEvent.FeatureURL.Path == "InsertRowsBefore" ||
             aEvent.FeatureURL.Path == "InsertRowsAfter" ||
             aEvent.FeatureURL.Path == "InsertColumnsBefore" ||
             aEvent.FeatureURL.Path == "InsertColumnsAfter" ||
             aEvent.FeatureURL.Path == "MergeCells" ||
             aEvent.FeatureURL.Path == "InsertObjectChart" ||
             aEvent.FeatureURL.Path == "InsertSection" ||
             aEvent.FeatureURL.Path == "InsertAnnotation" ||
             aEvent.FeatureURL.Path == "InsertPagebreak" ||
             aEvent.FeatureURL.Path == "InsertColumnBreak" ||
             aEvent.FeatureURL.Path == "HyperlinkDialog" ||
             aEvent.FeatureURL.Path == "InsertSymbol" ||
             aEvent.FeatureURL.Path == "InsertPage" ||
             aEvent.FeatureURL.Path == "DeletePage" ||
             aEvent.FeatureURL.Path == "DuplicatePage" ||
             aEvent.FeatureURL.Path == "DeleteRows" ||
             aEvent.FeatureURL.Path == "DeleteColumns" ||
             aEvent.FeatureURL.Path == "DeleteTable" ||
             aEvent.FeatureURL.Path == "SelectTable" ||
             aEvent.FeatureURL.Path == "EntireRow" ||
             aEvent.FeatureURL.Path == "EntireColumn" ||
             aEvent.FeatureURL.Path == "EntireCell" ||
             aEvent.FeatureURL.Path == "SortAscending" ||
             aEvent.FeatureURL.Path == "SortDescending" ||
             aEvent.FeatureURL.Path == "AcceptAllTrackedChanges" ||
             aEvent.FeatureURL.Path == "RejectAllTrackedChanges" ||
             aEvent.FeatureURL.Path == "AcceptTrackedChange" ||
             aEvent.FeatureURL.Path == "RejectTrackedChange" ||
             aEvent.FeatureURL.Path == "NextTrackedChange" ||
             aEvent.FeatureURL.Path == "PreviousTrackedChange" ||
             aEvent.FeatureURL.Path == "FormatGroup" ||
             aEvent.FeatureURL.Path == "ObjectBackOne" ||
             aEvent.FeatureURL.Path == "SendToBack" ||
             aEvent.FeatureURL.Path == "ObjectForwardOne" ||
             aEvent.FeatureURL.Path == "BringToFront" ||
             aEvent.FeatureURL.Path == "WrapRight" ||
             aEvent.FeatureURL.Path == "WrapThrough" ||
             aEvent.FeatureURL.Path == "WrapLeft" ||
             aEvent.FeatureURL.Path == "WrapIdeal" ||
             aEvent.FeatureURL.Path == "WrapOn" ||
             aEvent.FeatureURL.Path == "WrapOff" ||
             aEvent.FeatureURL.Path == "UpdateCurIndex" ||
             aEvent.FeatureURL.Path == "InsertCaptionDialog" ||
             aEvent.FeatureURL.Path == "MergeCells" ||
             aEvent.FeatureURL.Path == "SplitTable" ||
             aEvent.FeatureURL.Path == "SplitCell" ||
             aEvent.FeatureURL.Path == "DeleteNote" ||
             aEvent.FeatureURL.Path == "AcceptChanges" ||
             aEvent.FeatureURL.Path == "SetDefault" ||
             aEvent.FeatureURL.Path == "ParaLeftToRight" ||
             aEvent.FeatureURL.Path == "ParaRightToLeft" ||
             aEvent.FeatureURL.Path == "ParaspaceIncrease" ||
             aEvent.FeatureURL.Path == "ParaspaceDecrease" ||
             aEvent.FeatureURL.Path == "TableDialog" ||
             aEvent.FeatureURL.Path == "FormatCellDialog" ||
             aEvent.FeatureURL.Path == "FontDialog" ||
             aEvent.FeatureURL.Path == "ParagraphDialog" ||
             aEvent.FeatureURL.Path == "OutlineBullet" ||
             aEvent.FeatureURL.Path == "InsertIndexesEntry" ||
             aEvent.FeatureURL.Path == "TransformDialog" ||
             aEvent.FeatureURL.Path == "EditRegion" ||
             aEvent.FeatureURL.Path == "ThesaurusDialog" ||
             aEvent.FeatureURL.Path == "OutlineRight" ||
             aEvent.FeatureURL.Path == "OutlineLeft" ||
             aEvent.FeatureURL.Path == "OutlineDown" ||
             aEvent.FeatureURL.Path == "OutlineUp" ||
             aEvent.FeatureURL.Path == "FormatArea" ||
             aEvent.FeatureURL.Path == "FormatLine" ||
             aEvent.FeatureURL.Path == "FormatColumns" ||
             aEvent.FeatureURL.Path == "Watermark" ||
             aEvent.FeatureURL.Path == "InsertBreak" ||
             aEvent.FeatureURL.Path == "InsertEndnote" ||
             aEvent.FeatureURL.Path == "InsertFootnote" ||
             aEvent.FeatureURL.Path == "InsertReferenceField" ||
             aEvent.FeatureURL.Path == "InsertBookmark" ||
             aEvent.FeatureURL.Path == "InsertAuthoritiesEntry" ||
             aEvent.FeatureURL.Path == "InsertMultiIndex" ||
             aEvent.FeatureURL.Path == "InsertField" ||
             aEvent.FeatureURL.Path == "PageNumberWizard" ||
             aEvent.FeatureURL.Path == "InsertPageNumberField" ||
             aEvent.FeatureURL.Path == "InsertPageCountField" ||
             aEvent.FeatureURL.Path == "InsertDateField" ||
             aEvent.FeatureURL.Path == "InsertTitleField" ||
             aEvent.FeatureURL.Path == "InsertFieldCtrl" ||
             aEvent.FeatureURL.Path == "CharmapControl" ||
             aEvent.FeatureURL.Path == "EnterGroup" ||
             aEvent.FeatureURL.Path == "LeaveGroup" ||
             aEvent.FeatureURL.Path == "Combine" ||
             aEvent.FeatureURL.Path == "Merge" ||
             aEvent.FeatureURL.Path == "Dismantle" ||
             aEvent.FeatureURL.Path == "Substract" ||
             aEvent.FeatureURL.Path == "DistributeSelection" ||
             aEvent.FeatureURL.Path == "Intersect" ||
             aEvent.FeatureURL.Path == "ResetAttributes" ||
             aEvent.FeatureURL.Path == "IncrementIndent" ||
             aEvent.FeatureURL.Path == "DecrementIndent" ||
             aEvent.FeatureURL.Path == "EditHeaderAndFooter" ||
             aEvent.FeatureURL.Path == "InsertSparkline" ||
             aEvent.FeatureURL.Path == "DeleteSparkline" ||
             aEvent.FeatureURL.Path == "DeleteSparklineGroup" ||
             aEvent.FeatureURL.Path == "EditSparklineGroup" ||
             aEvent.FeatureURL.Path == "EditSparkline" ||
             aEvent.FeatureURL.Path == "GroupSparklines" ||
             aEvent.FeatureURL.Path == "UngroupSparklines" ||
             aEvent.FeatureURL.Path == "FormatSparklineMenu" ||
             aEvent.FeatureURL.Path == "NumberFormatDecDecimals" ||
             aEvent.FeatureURL.Path == "NumberFormatIncDecimals" ||
             aEvent.FeatureURL.Path == "Protect" ||
             aEvent.FeatureURL.Path == "UnsetCellsReadOnly" ||
             aEvent.FeatureURL.Path == "ContentControlProperties" ||
             aEvent.FeatureURL.Path == "InsertCheckboxContentControl" ||
             aEvent.FeatureURL.Path == "InsertContentControl" ||
             aEvent.FeatureURL.Path == "InsertDateContentControl" ||
             aEvent.FeatureURL.Path == "InsertDropdownContentControl" ||
             aEvent.FeatureURL.Path == "InsertPlainTextContentControl" ||
             aEvent.FeatureURL.Path == "InsertPictureContentControl")
    {
        aBuffer.append(aEvent.IsEnabled ? std::u16string_view(u"enabled") : std::u16string_view(u"disabled"));
    }
    else if (aEvent.FeatureURL.Path == "AssignLayout" ||
             aEvent.FeatureURL.Path == "StatusSelectionMode" ||
             aEvent.FeatureURL.Path == "Signature" ||
             aEvent.FeatureURL.Path == "SelectionMode" ||
             aEvent.FeatureURL.Path == "StatusBarFunc")
    {
        sal_Int32 aInt32;

        if (aEvent.IsEnabled && (aEvent.State >>= aInt32))
        {
            aBuffer.append(aInt32);
        }
    }
    else if (aEvent.FeatureURL.Path == "TransformPosX" ||
             aEvent.FeatureURL.Path == "TransformPosY" ||
             aEvent.FeatureURL.Path == "TransformWidth" ||
             aEvent.FeatureURL.Path == "TransformHeight")
    {
        const SfxViewShell* pViewShell = SfxViewShell::Current();
        if (aEvent.IsEnabled && pViewShell && pViewShell->isLOKMobilePhone())
        {
            boost::property_tree::ptree aTree;
            boost::property_tree::ptree aState;
            OUString aStr(aEvent.FeatureURL.Complete);

            aTree.put("commandName", aStr.toUtf8().getStr());
            pViewFrame->GetBindings().QueryControlState(nSID, aState);
            aTree.add_child("state", aState);

            aBuffer.setLength(0);
            std::stringstream aStream;
            boost::property_tree::write_json(aStream, aTree);
            aBuffer.appendAscii(aStream.str().c_str());
        }
    }
    else if (aEvent.FeatureURL.Path == "StatusDocPos" ||
             aEvent.FeatureURL.Path == "RowColSelCount" ||
             aEvent.FeatureURL.Path == "StatusPageStyle" ||
             aEvent.FeatureURL.Path == "StateWordCount" ||
             aEvent.FeatureURL.Path == "PageStyleName" ||
             aEvent.FeatureURL.Path == "PageStatus" ||
             aEvent.FeatureURL.Path == "LayoutStatus" ||
             aEvent.FeatureURL.Path == "Scale" ||
             aEvent.FeatureURL.Path == "Context")
    {
        OUString aString;

        if (aEvent.IsEnabled && (aEvent.State >>= aString))
        {
            aBuffer.append(aString);
        }
    }
    else if (aEvent.FeatureURL.Path == "StateTableCell")
    {
        if (aEvent.IsEnabled)
        {
            if (const SfxStringItem* pSvxStatusItem = dynamic_cast<const SfxStringItem*>(pState))
                aBuffer.append(pSvxStatusItem->GetValue());
        }
    }
    else if (aEvent.FeatureURL.Path == "InsertMode" ||
             aEvent.FeatureURL.Path == "WrapText" ||
             aEvent.FeatureURL.Path == "NumberFormatCurrency" ||
             aEvent.FeatureURL.Path == "NumberFormatPercent" ||
             aEvent.FeatureURL.Path == "NumberFormatDecimal" ||
             aEvent.FeatureURL.Path == "NumberFormatDate" ||
             aEvent.FeatureURL.Path == "ShowResolvedAnnotations")
    {
        bool aBool;

        if (aEvent.IsEnabled && (aEvent.State >>= aBool))
        {
            aBuffer.append(OUString::boolean(aBool));
        }
    }
    else if (aEvent.FeatureURL.Path == "ToggleMergeCells")
    {
        bool aBool;

        if (aEvent.IsEnabled && (aEvent.State >>= aBool))
        {
            aBuffer.append(OUString::boolean(aBool));
        }
        else
        {
            aBuffer.append("disabled");
        }
    }
    else if (aEvent.FeatureURL.Path == "Position" ||
             aEvent.FeatureURL.Path == "FreezePanesColumn" ||
             aEvent.FeatureURL.Path == "FreezePanesRow")
    {
        css::awt::Point aPoint;

        if (aEvent.IsEnabled && (aEvent.State >>= aPoint))
        {
            aBuffer.append( OUString::number(aPoint.X) + " / " + OUString::number(aPoint.Y));
        }
    }
    else if (aEvent.FeatureURL.Path == "Size")
    {
        css::awt::Size aSize;

        if (aEvent.IsEnabled && (aEvent.State >>= aSize))
        {
            aBuffer.append( OUString::number(aSize.Width) + " x " + OUString::number(aSize.Height) );
        }
    }
    else if (aEvent.FeatureURL.Path == "LanguageStatus" ||
             aEvent.FeatureURL.Path == "StatePageNumber")
    {
        css::uno::Sequence< OUString > aSeq;

        if (aEvent.IsEnabled)
        {
            OUString sValue;
            if (aEvent.State >>= sValue)
            {
                aBuffer.append(sValue);
            }
            else if (aEvent.State >>= aSeq)
            {
                aBuffer.append(aSeq[0]);
            }
        }
    }
    else if (aEvent.FeatureURL.Path == "InsertPageHeader" ||
             aEvent.FeatureURL.Path == "InsertPageFooter")
    {
        if (aEvent.IsEnabled)
        {
            css::uno::Sequence< OUString > aSeq;
            if (aEvent.State >>= aSeq)
            {
                aBuffer.append(u'{');
                for (sal_Int32 itSeq = 0; itSeq < aSeq.getLength(); itSeq++)
                {
                    aBuffer.append("\"" + aSeq[itSeq]);
                    if (itSeq != aSeq.getLength() - 1)
                        aBuffer.append("\":true,");
                    else
                        aBuffer.append("\":true");
                }
                aBuffer.append(u'}');
            }
        }
    }
    else if (aEvent.FeatureURL.Path == "TableColumWidth" ||
             aEvent.FeatureURL.Path == "TableRowHeight")
    {
        sal_Int32 nValue;
        if (aEvent.State >>= nValue)
        {
            float nScaleValue = 1000.0;
            nValue *= nScaleValue;
            sal_Int32 nConvertedValue = o3tl::convert(nValue, o3tl::Length::twip, o3tl::Length::in);
            aBuffer.append(nConvertedValue / nScaleValue);
        }
    }
    else
    {
        // Try to send JSON state version
        SfxLokHelper::sendUnoStatus(pViewFrame->GetViewShell(), pState);

        return;
    }

    OUString payload = aBuffer.makeStringAndClear();
    if (const SfxViewShell* pViewShell = pViewFrame->GetViewShell())
        pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_STATE_CHANGED, payload.toUtf8());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
