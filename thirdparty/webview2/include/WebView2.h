// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license found in the
// LICENSE file distributed with the WebView2 SDK NuGet package.
//
// STUB: Minimal COM interface definitions for WebViewWidget_win.cpp.
// Replace with the real WebView2.h from the Microsoft.Web.WebView2 NuGet
// package for a fully functional binary. See README.md for instructions.

#pragma once

#ifndef __webview2_h__
#define __webview2_h__

#include <objbase.h>
#include <EventToken.h>

// Enums
typedef enum COREWEBVIEW2_PROCESS_FAILED_KIND {
    COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED = 0,
    COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED = 1,
    COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE = 2,
} COREWEBVIEW2_PROCESS_FAILED_KIND;

typedef enum COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION {
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW = 0,
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL = 1,
} COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION;

// Forward declarations
typedef interface ICoreWebView2 ICoreWebView2;
typedef interface ICoreWebView2Controller ICoreWebView2Controller;
typedef interface ICoreWebView2Environment ICoreWebView2Environment;
typedef interface ICoreWebView2Settings ICoreWebView2Settings;
typedef interface ICoreWebView2NavigationCompletedEventArgs ICoreWebView2NavigationCompletedEventArgs;
typedef interface ICoreWebView2ProcessFailedEventArgs ICoreWebView2ProcessFailedEventArgs;
typedef interface ICoreWebView2ServerCertificateErrorDetectedEventArgs ICoreWebView2ServerCertificateErrorDetectedEventArgs;

// --- Interface definitions ---

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044770")
ICoreWebView2NavigationCompletedEventArgs : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL *isSuccess) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044771")
ICoreWebView2ProcessFailedEventArgs : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_ProcessFailedKind(COREWEBVIEW2_PROCESS_FAILED_KIND *kind) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044772")
ICoreWebView2Settings : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL areDefaultContextMenusEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL *) = 0;
};

typedef interface ICoreWebView2NavigationCompletedEventHandler ICoreWebView2NavigationCompletedEventHandler;
typedef interface ICoreWebView2ProcessFailedEventHandler ICoreWebView2ProcessFailedEventHandler;

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044773")
ICoreWebView2 : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings **settings) = 0;
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reload() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(
        ICoreWebView2NavigationCompletedEventHandler *handler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(
        ICoreWebView2ProcessFailedEventHandler *handler, EventRegistrationToken *token) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044774")
ICoreWebView2Controller : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

typedef interface ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044775")
ICoreWebView2Environment : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(
        HWND parentWindow,
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *handler) = 0;
};

// --- Handler (callback) interfaces ---

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044776")
ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *createdEnvironment) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044777")
ICoreWebView2CreateCoreWebView2ControllerCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *createdController) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044778")
ICoreWebView2NavigationCompletedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044779")
ICoreWebView2ProcessFailedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2ProcessFailedEventArgs *args) = 0;
};

// IID constants for handler interfaces (MinGW's MIDL_INTERFACE discards the UUID)
static const IID IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x76}};
static const IID IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x77}};
static const IID IID_ICoreWebView2NavigationCompletedEventHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x78}};
static const IID IID_ICoreWebView2ProcessFailedEventHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x79}};

// Global function: creates a WebView2 environment with options.
STDAPI CreateCoreWebView2EnvironmentWithOptions(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    IUnknown *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *environmentCreatedHandler);

#endif // __webview2_h__
