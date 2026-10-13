#pragma once
#include <windows.h>
#include <inspectable.h>
// Minimal Windows ABI declarations absent from MinGW 13's SDK.
// Reference: Microsoft win32metadata, winrt/windows.graphics.capture.h.
namespace clipsapp::wgc {
struct Size { INT32 width, height; };
struct Frame : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Surface(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Time(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ContentSize(Size*) = 0;
};
struct Item : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Name(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSize(Size*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddClosed(IUnknown*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveClosed(INT64) = 0;
};
struct Session : IInspectable { virtual HRESULT STDMETHODCALLTYPE Start() = 0; };
struct Pool : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Recreate(IInspectable*, int, int, Size) = 0;
    virtual HRESULT STDMETHODCALLTYPE Next(Frame**) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddArrived(IUnknown*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveArrived(INT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSession(Item*, Session**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Dispatcher(IInspectable**) = 0;
};
struct PoolFactory : IInspectable { virtual HRESULT STDMETHODCALLTYPE Create(IInspectable*, int, int, Size, Pool**) = 0; };
struct ItemFactory : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ForWindow(HWND, REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ForMonitor(HMONITOR, REFIID, void**) = 0;
};
struct DxgiAccess : IUnknown { virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID, void**) = 0; };
struct Closable : IInspectable { virtual HRESULT STDMETHODCALLTYPE Close() = 0; };
struct Cursor : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Get(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Set(boolean) = 0;
};
struct Border : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Get(boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Set(boolean) = 0;
};
// IAsyncOperation<AppCapabilityAccessStatus> and IAsyncInfo ABI.
struct AccessOperation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE SetCompleted(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCompleted(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Results(INT32*) = 0;
};
struct AccessFactory : IInspectable { virtual HRESULT STDMETHODCALLTYPE Request(INT32, AccessOperation**) = 0; };
struct AsyncInfo : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Status(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Error(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
inline const GUID itemId{0x79c3f95b, 0x31f7, 0x4ec2, {0xa4,0x64,0x63,0x2e,0xf5,0xd3,0x07,0x60}};
inline const GUID itemFactoryId{0x3628e81b, 0x3cac, 0x4c60, {0xb7,0xf4,0x23,0xce,0x0e,0x0c,0x33,0x56}};
inline const GUID poolFactoryId{0x589b103f, 0x6bbc, 0x5df5, {0xa9,0x91,0x02,0xe2,0x8b,0x3b,0x66,0xd5}};
inline const GUID dxgiAccessId{0xa9b3d012, 0x3df2, 0x4ee3, {0xb8,0xd1,0x86,0x95,0xf4,0x57,0xd3,0xc1}};
inline const GUID closableId{0x30d5a829, 0x7fa4, 0x4026, {0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e}};
inline const GUID cursorId{0x2c39ae40, 0x7d2e, 0x5044, {0x80,0x4e,0x8b,0x67,0x99,0xd4,0xcf,0x9e}};
inline const GUID borderId{0xf2cdd966, 0x22ae, 0x5ea1, {0x95,0x96,0x3a,0x28,0x93,0x44,0xc3,0xbe}};
inline const GUID accessFactoryId{0x743ed370, 0x06ec, 0x5040, {0xa5,0x8a,0x90,0x1f,0x0f,0x75,0x70,0x95}};
inline const GUID asyncInfoId{0x00000036, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
}
