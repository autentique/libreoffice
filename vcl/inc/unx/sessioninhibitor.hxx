/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_VCL_INC_UNX_SCREENSAVERINHIBITOR_HXX
#define INCLUDED_VCL_INC_UNX_SCREENSAVERINHIBITOR_HXX

#include <X11/Xlib.h>
#include <X11/Xmd.h>

#include <rtl/ustring.hxx>
#include <vcl/dllapi.h>

#include <optional>
#include <string_view>

enum ApplicationInhibitFlags
{
    APPLICATION_INHIBIT_LOGOUT = (1 << 0),
    APPLICATION_INHIBIT_SWITCH = (1 << 1),
    APPLICATION_INHIBIT_SUSPEND = (1 << 2),
    APPLICATION_INHIBIT_IDLE = (1 << 3) // Inhibit the session being marked as idle
};

class VCL_PLUGIN_PUBLIC SessionManagerInhibitor
{
public:
    void inhibit(bool bInhibit, std::u16string_view sReason, ApplicationInhibitFlags eType,
                 unsigned int window_system_id, std::optional<Display*> pDisplay,
                 const char* application_id = nullptr);

private:
    // These are all used as guint, however this header may be included
    // in kde/tde/etc backends, where we would ideally avoid having
    // any glib dependencies, hence the direct use of unsigned int.
    std::optional<unsigned int> mnFDOSSCookie; // FDO ScreenSaver Inhibit
    std::optional<unsigned int> mnFDOPMCookie; // FDO PowerManagement Inhibit
    std::optional<unsigned int> mnGSMCookie;
    std::optional<unsigned int> mnMSMCookie;

    std::optional<int> mnXScreenSaverTimeout;

#if !defined(__sun)
    BOOL mbDPMSWasEnabled;
    CARD16 mnDPMSStandbyTimeout;
    CARD16 mnDPMSSuspendTimeout;
    CARD16 mnDPMSOffTimeout;
#endif

    // There are a bunch of different dbus based inhibition APIs. Some call
    // themselves ScreenSaver inhibition, some are PowerManagement inhibition,
    // but they appear to have the same effect. There doesn't appear to be one
    // all encompassing standard, hence we should just try all of them.
    //
    // The current APIs we have: (note: the list of supported environments is incomplete)
    // FDSSO: org.freedesktop.ScreenSaver::Inhibit - appears to be supported only by KDE?
    // FDOPM: org.freedesktop.PowerManagement.Inhibit::Inhibit - XFCE, (KDE) ?
    //        (KDE: doesn't inhibit screensaver, but does inhibit PowerManagement)
    // GSM: org.gnome.SessionManager::Inhibit - gnome 3
    // MSM: org.mate.Sessionmanager::Inhibit - Mate <= 1.10, is identical to GSM
    //       (This is replaced by the GSM interface from Mate 1.12 onwards)
    //
    // Note: the Uninhibit call has different spelling in FDOSS (UnInhibit) vs GSM (Uninhibit)
    void inhibitFDOSS(bool bInhibit, const char* appname, const char* reason);
    void inhibitFDOPM(bool bInhibit, const char* appname, const char* reason);
    void inhibitGSM(bool bInhibit, const char* appname, const char* reason,
                    ApplicationInhibitFlags eType, unsigned int window_system_id);
    void inhibitMSM(bool bInhibit, const char* appname, const char* reason,
                    ApplicationInhibitFlags eType, unsigned int window_system_id);

    void inhibitXScreenSaver(bool bInhibit, Display* pDisplay);
    static void inhibitXAutoLock(bool bInhibit, Display* pDisplay);
    void inhibitDPMS(bool bInhibit, Display* pDisplay);
};

#endif // INCLUDED_VCL_INC_UNX_SCREENSAVERINHIBITOR_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
