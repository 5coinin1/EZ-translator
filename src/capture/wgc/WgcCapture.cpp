#include "capture/wgc/WgcCapture.h"

#include <algorithm>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <eventtoken.h>
// initguid.h biến các DEFINE_GUID của dxgi.h/d3d11.h thành định nghĩa thật, nên
// IID của DXGI/D3D không thể bị gõ nhầm tay.
#include <initguid.h>
#include <dxgi.h>
#include <d3d11.h>

// Do d3d11.dll export nhưng header MinGW không khai báo.
extern "C" HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice* dxgiDevice,
                                                               IInspectable** graphicsDevice);

namespace EZTranslator {
namespace {

typedef unsigned char WrtBoolean;

struct SizeInt32
{
    INT32 Width = 0;
    INT32 Height = 0;
};

// --- Windows.Graphics.Capture -------------------------------------------------
// SDK MinGW chỉ có IGraphicsCaptureSession, phần còn lại của namespace được khai
// báo thủ công ở đây. Bố cục vtable theo đúng IDL của Windows SDK (IUnknown,
// IInspectable, rồi các method theo thứ tự khai báo).

struct IGraphicsCaptureItem;
struct IDirect3D11CaptureFrame;
struct IDirect3D11CaptureFramePool;
struct IGraphicsCaptureSession;

MIDL_INTERFACE("79c3f95b-31f7-4ec2-a464-632ef5d30760")
IGraphicsCaptureItem : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(SizeInt32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) = 0;
};

MIDL_INTERFACE("fa50c623-38da-4b32-acf3-fa9734ad800e")
IDirect3D11CaptureFrame : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Surface(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SystemRelativeTime(INT64* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentSize(SizeInt32* value) = 0;
};

MIDL_INTERFACE("24eb6d22-1975-422e-82e7-780dbd8ddf24")
IDirect3D11CaptureFramePool : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Recreate(IInspectable* device, INT32 pixelFormat,
                                               INT32 numberOfBuffers, SizeInt32 size) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetNextFrame(IDirect3D11CaptureFrame** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameArrived(IUnknown* handler,
                                                       EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameArrived(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCaptureSession(IGraphicsCaptureItem* item,
                                                           IGraphicsCaptureSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(IInspectable** value) = 0;
};

MIDL_INTERFACE("7784056a-67aa-4d53-ae54-1088d5a8ca21")
IDirect3D11CaptureFramePoolStatics : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Create(IInspectable* device, INT32 pixelFormat,
                                             INT32 numberOfBuffers, SizeInt32 size,
                                             IDirect3D11CaptureFramePool** result) = 0;
};

MIDL_INTERFACE("589b103f-6bbc-5df5-a991-02e28b3b66d5")
IDirect3D11CaptureFramePoolStatics2 : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateFreeThreaded(IInspectable* device, INT32 pixelFormat,
                                                         INT32 numberOfBuffers, SizeInt32 size,
                                                         IDirect3D11CaptureFramePool** result) = 0;
};

MIDL_INTERFACE("814e42a9-f70f-4ad7-939b-fddcc6eb880d")
IGraphicsCaptureSession : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE StartCapture() = 0;
};

MIDL_INTERFACE("2c39ae40-7d2e-5044-804e-8b6799d4cf9e")
IGraphicsCaptureSession2 : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(WrtBoolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(WrtBoolean value) = 0;
};

MIDL_INTERFACE("f2cdd966-22ae-5ea1-9596-3a289344c3be")
IGraphicsCaptureSession3 : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_IsBorderRequired(WrtBoolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBorderRequired(WrtBoolean value) = 0;
};

// --- desktop interop ----------------------------------------------------------

MIDL_INTERFACE("3628e81b-3cac-4c60-b7f4-23ce0e0c3356")
IGraphicsCaptureItemInterop : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID riid, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID riid,
                                                       void** result) = 0;
};

MIDL_INTERFACE("a9b3d012-3df2-4ee3-b8d1-8695f457d3c1")
IDirect3DDxgiInterfaceAccess : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};

const GUID kIID_IGraphicsCaptureItem = {
    0x79c3f95b, 0x31f7, 0x4ec2, {0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60}};
const GUID kIID_IGraphicsCaptureItemInterop = {
    0x3628e81b, 0x3cac, 0x4c60, {0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56}};
const GUID kIID_IGraphicsCaptureSession2 = {
    0x2c39ae40, 0x7d2e, 0x5044, {0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e}};
const GUID kIID_IGraphicsCaptureSession3 = {
    0xf2cdd966, 0x22ae, 0x5ea1, {0x95, 0x96, 0x3a, 0x28, 0x93, 0x44, 0xc3, 0xbe}};
const GUID kIID_IDirect3D11CaptureFramePool = {
    0x24eb6d22, 0x1975, 0x422e, {0x82, 0xe7, 0x78, 0x0d, 0xbd, 0x8d, 0xdf, 0x24}};
const GUID kIID_IDirect3D11CaptureFramePoolStatics = {
    0x7784056a, 0x67aa, 0x4d53, {0xae, 0x54, 0x10, 0x88, 0xd5, 0xa8, 0xca, 0x21}};
const GUID kIID_IDirect3D11CaptureFramePoolStatics2 = {
    0x589b103f, 0x6bbc, 0x5df5, {0xa9, 0x91, 0x02, 0xe2, 0x8b, 0x3b, 0x66, 0xd5}};
const GUID kIID_IDirect3DDxgiInterfaceAccess = {
    0xa9b3d012, 0x3df2, 0x4ee3, {0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1}};
// IID_ID3D11Texture2D và IID_IDXGIDevice đến từ <initguid.h> + dxgi.h/d3d11.h.

const wchar_t* kGraphicsCaptureItemClass = L"Windows.Graphics.Capture.GraphicsCaptureItem";
const wchar_t* kFramePoolClass = L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";

class HString
{
public:
    explicit HString(const wchar_t* text)
    {
        WindowsCreateString(text, UINT32(wcslen(text)), &m_value);
    }
    ~HString()
    {
        if (m_value)
            WindowsDeleteString(m_value);
    }

    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;

    [[nodiscard]] HSTRING get() const { return m_value; }
    [[nodiscard]] bool valid() const { return m_value != nullptr; }

private:
    HSTRING m_value = nullptr;
};

template <class T>
void release(T*& pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

// Cố gắng tạo WinRT apartment cho thread này. Trả true nếu thread dùng được WinRT
// sau đó; `*ownInit` cho biết có phải gọi RoUninitialize khi ra không. Apartment
// đã tồn tại (RPC_E_CHANGED_MODE) vẫn ổn vì các object capture là agile.
bool ensureApartment(bool* ownInit)
{
    *ownInit = false;

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (hr == S_OK) {
        *ownInit = true;
        return true;
    }
    if (hr == S_FALSE)
        return true;

    // RPC_E_CHANGED_MODE: thread đang ở STA (Qt hay làm vậy trên GUI thread).
    // WinRT chưa được init nên gọi vào sẽ crash — init lại dạng single-threaded.
    if (hr == RPC_E_CHANGED_MODE) {
        hr = RoInitialize(RO_INIT_SINGLETHREADED);
        if (hr == S_OK) {
            *ownInit = true;
            return true;
        }
        if (hr == S_FALSE)
            return true;
    }
    return false;
}

} // namespace

struct WgcCapture::Impl
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IInspectable* winrtDevice = nullptr;
    IGraphicsCaptureItem* item = nullptr;
    IDirect3D11CaptureFramePool* pool = nullptr;
    IGraphicsCaptureSession* session = nullptr;
    ID3D11Texture2D* staging = nullptr;
    int stagingWidth = 0;
    int stagingHeight = 0;
    SizeInt32 size;
    bool ownApartment = false;
    bool started = false;

    bool ensureStaging(int width, int height)
    {
        if (staging && stagingWidth == width && stagingHeight == height)
            return true;
        release(staging);

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = UINT(width);
        desc.Height = UINT(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
            release(staging);
            stagingWidth = stagingHeight = 0;
            return false;
        }
        stagingWidth = width;
        stagingHeight = height;
        return true;
    }

    void destroy()
    {
        release(session);
        release(pool);
        release(item);
        release(staging);
        release(winrtDevice);
        release(context);
        release(device);
        stagingWidth = stagingHeight = 0;
        size = SizeInt32();
        started = false;
        if (ownApartment) {
            RoUninitialize();
            ownApartment = false;
        }
    }
};

WgcCapture::WgcCapture() = default;

WgcCapture::~WgcCapture()
{
    stop();
}

bool WgcCapture::isSupported()
{
    bool ownInit = false;
    if (!ensureApartment(&ownInit))
        return false;

    // Activation factory của GraphicsCaptureItem chỉ tồn tại khi OS có
    // Windows.Graphics.Capture, nên lấy được nó là phép thử khả dụng.
    bool supported = false;
    HString className(kGraphicsCaptureItemClass);
    IGraphicsCaptureItemInterop* interop = nullptr;
    if (className.valid()
        && SUCCEEDED(RoGetActivationFactory(className.get(), kIID_IGraphicsCaptureItemInterop,
                                            reinterpret_cast<void**>(&interop)))
        && interop) {
        supported = true;
        interop->Release();
    }

    if (ownInit)
        RoUninitialize();
    return supported;
}

bool WgcCapture::start(uintptr_t windowId)
{
    stop();
    if (!windowId || !IsWindow(reinterpret_cast<HWND>(windowId)))
        return false;

    auto* impl = new Impl();
    m_impl = impl;

    if (!ensureApartment(&impl->ownApartment)) {
        stop();
        return false;
    }

    // 1. Capture item cho cửa sổ (hoạt động cả với cửa sổ DirectX).
    //
    // THỨ TỰ QUAN TRỌNG: phải tạo item TRƯỚC khi có bất kỳ D3D11 device nào
    // trong process này. Tạo device trước khiến CreateForWindow() trên cửa sổ
    // của process khác nhảy vào vùng nhớ chưa map.
    IGraphicsCaptureItemInterop* interop = nullptr;
    HString itemClass(kGraphicsCaptureItemClass);
    if (!itemClass.valid()
        || FAILED(RoGetActivationFactory(itemClass.get(), kIID_IGraphicsCaptureItemInterop,
                                         reinterpret_cast<void**>(&interop)))
        || !interop) {
        stop();
        return false;
    }
    const HRESULT itemHr = interop->CreateForWindow(reinterpret_cast<HWND>(windowId),
                                                    kIID_IGraphicsCaptureItem,
                                                    reinterpret_cast<void**>(&impl->item));
    interop->Release();
    if (FAILED(itemHr) || !impl->item) {
        stop();
        return false;
    }
    impl->item->get_Size(&impl->size);
    if (impl->size.Width <= 0 || impl->size.Height <= 0) {
        stop();
        return false;
    }

    // 2. D3D11 device. Bắt buộc hỗ trợ BGRA cho interop WinRT.
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                      nullptr, 0, D3D11_SDK_VERSION, &impl->device, nullptr, &impl->context);
    if (!impl->device) {
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                          nullptr, 0, D3D11_SDK_VERSION, &impl->device, nullptr, &impl->context);
    }
    if (!impl->device || !impl->context) {
        stop();
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(impl->device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice)))
        || !dxgiDevice) {
        stop();
        return false;
    }
    const HRESULT deviceHr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice, &impl->winrtDevice);
    dxgiDevice->Release();
    if (FAILED(deviceHr) || !impl->winrtDevice) {
        stop();
        return false;
    }

    // 3. Frame pool. Bản free-threaded không cần dispatcher queue nên chạy được
    //    trên worker thread thuần.
    HString poolClass(kFramePoolClass);
    if (!poolClass.valid()) {
        stop();
        return false;
    }

    IDirect3D11CaptureFramePoolStatics2* statics2 = nullptr;
    RoGetActivationFactory(poolClass.get(), kIID_IDirect3D11CaptureFramePoolStatics2,
                           reinterpret_cast<void**>(&statics2));
    if (statics2) {
        statics2->CreateFreeThreaded(impl->winrtDevice, DXGI_FORMAT_B8G8R8A8_UNORM, 2, impl->size,
                                     &impl->pool);
        statics2->Release();
    } else {
        IDirect3D11CaptureFramePoolStatics* statics = nullptr;
        RoGetActivationFactory(poolClass.get(), kIID_IDirect3D11CaptureFramePoolStatics,
                               reinterpret_cast<void**>(&statics));
        if (statics) {
            statics->Create(impl->winrtDevice, DXGI_FORMAT_B8G8R8A8_UNORM, 2, impl->size,
                            &impl->pool);
            statics->Release();
        }
    }
    if (!impl->pool) {
        stop();
        return false;
    }

    // 4. Session: không cursor, không viền vàng nếu OS cho phép.
    if (FAILED(impl->pool->CreateCaptureSession(impl->item, &impl->session))
        || !impl->session) {
        stop();
        return false;
    }
    IGraphicsCaptureSession2* session2 = nullptr;
    if (SUCCEEDED(impl->session->QueryInterface(kIID_IGraphicsCaptureSession2,
                                                reinterpret_cast<void**>(&session2)))
        && session2) {
        session2->put_IsCursorCaptureEnabled(0);
        session2->Release();
    }
    IGraphicsCaptureSession3* session3 = nullptr;
    if (SUCCEEDED(impl->session->QueryInterface(kIID_IGraphicsCaptureSession3,
                                                reinterpret_cast<void**>(&session3)))
        && session3) {
        session3->put_IsBorderRequired(0);
        session3->Release();
    }
    if (FAILED(impl->session->StartCapture())) {
        stop();
        return false;
    }

    impl->started = true;
    return true;
}

bool WgcCapture::isValid() const
{
    return m_impl && m_impl->started && m_impl->pool && m_impl->device;
}

void WgcCapture::stop()
{
    if (!m_impl)
        return;
    m_impl->destroy();
    delete m_impl;
    m_impl = nullptr;
}

WgcCapture::Frame WgcCapture::grab(int regionX, int regionY, int regionWidth, int regionHeight)
{
    Impl* impl = m_impl;
    if (!isValid() || regionWidth <= 0 || regionHeight <= 0)
        return {};

    IDirect3D11CaptureFrame* frame = nullptr;
    if (FAILED(impl->pool->TryGetNextFrame(&frame)) || !frame)
        return {};
    if (!impl->device || !impl->context) {
        frame->Release();
        return {};
    }

    // Cửa sổ có thể đã bị resize từ lúc session bắt đầu.
    SizeInt32 content;
    if (SUCCEEDED(frame->get_ContentSize(&content)) && content.Width > 0 && content.Height > 0
        && (content.Width != impl->size.Width || content.Height != impl->size.Height)) {
        impl->size = content;
        impl->pool->Recreate(impl->winrtDevice, DXGI_FORMAT_B8G8R8A8_UNORM, 2, impl->size);
    }

    Frame result;
    IInspectable* surface = nullptr;
    if (SUCCEEDED(frame->get_Surface(&surface)) && surface) {
        IDirect3DDxgiInterfaceAccess* access = nullptr;
        if (SUCCEEDED(surface->QueryInterface(kIID_IDirect3DDxgiInterfaceAccess,
                                              reinterpret_cast<void**>(&access)))
            && access) {
            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(access->GetInterface(IID_ID3D11Texture2D,
                                               reinterpret_cast<void**>(&texture)))
                && texture) {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);

                const int x = std::max(0, regionX);
                const int y = std::max(0, regionY);
                const int width = std::min(regionWidth, int(desc.Width) - x);
                const int height = std::min(regionHeight, int(desc.Height) - y);
                if (width > 0 && height > 0 && impl->ensureStaging(width, height)) {
                    const D3D11_BOX box{UINT(x), UINT(y), 0, UINT(x + width), UINT(y + height), 1};
                    impl->context->CopySubresourceRegion(impl->staging, 0, 0, 0, 0, texture, 0,
                                                         &box);

                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (SUCCEEDED(impl->context->Map(impl->staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                        // BGRA8, top-down.
                        result.width = width;
                        result.height = height;
                        result.stride = int(mapped.RowPitch);
                        const uint8_t* source = reinterpret_cast<const uint8_t*>(mapped.pData);
                        result.pixels.assign(source, source + size_t(mapped.RowPitch) * size_t(height));
                        impl->context->Unmap(impl->staging, 0);
                    }
                }
                texture->Release();
            }
            access->Release();
        }
        surface->Release();
    }

    frame->Release();
    return result;
}

WgcCapture::Frame WgcCapture::grabWait(int regionX, int regionY, int regionWidth, int regionHeight,
                                       int timeoutMs)
{
    const int step = 10;
    for (int waited = 0; waited <= std::max(0, timeoutMs); waited += step) {
        if (Frame frame = grab(regionX, regionY, regionWidth, regionHeight); !frame.isEmpty())
            return frame;
        Sleep(DWORD(step));
    }
    return {};
}

} // namespace EZTranslator

#else // !_WIN32

namespace EZTranslator {

struct WgcCapture::Impl
{
};

WgcCapture::WgcCapture() = default;
WgcCapture::~WgcCapture() = default;

bool WgcCapture::isSupported() { return false; }

bool WgcCapture::start(uintptr_t windowId)
{
    (void)windowId;
    return false;
}

bool WgcCapture::isValid() const { return false; }

void WgcCapture::stop() {}

WgcCapture::Frame WgcCapture::grab(int regionX, int regionY, int regionWidth, int regionHeight)
{
    (void)regionX;
    (void)regionY;
    (void)regionWidth;
    (void)regionHeight;
    return {};
}

WgcCapture::Frame WgcCapture::grabWait(int regionX, int regionY, int regionWidth, int regionHeight,
                                       int timeoutMs)
{
    (void)regionX;
    (void)regionY;
    (void)regionWidth;
    (void)regionHeight;
    (void)timeoutMs;
    return {};
}

} // namespace EZTranslator

#endif // _WIN32
