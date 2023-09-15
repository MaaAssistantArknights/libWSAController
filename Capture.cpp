#include "pch.h"

#include "libWSAController.h"
using namespace libWSAController;
#include "Capture.h"
#include "CallbackLogger.h"

bool WGCCapture::Attach(HWND hwnd)
{
    LogTraceFunction;

    Release();
    m_hwnd = hwnd;
    m_locker = std::make_unique<std::mutex>();
    m_recieved = std::make_unique<std::atomic_bool>();

    HRESULT result = S_OK;

    result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, m_native_device.put(), nullptr, m_context.put());
    if (FAILED(result)) {
        callback("D3D11CreateDevice", "ERR", std::format("GetLastError() = ", GetLastError()));
        return false;
    }

    auto fill_error = [](std::string_view fn_name) {
        callback(fn_name, "ERR", std::format("GetLastError() = {}", fn_name, GetLastError()));
        };

    if (hwnd == NULL) {
        fill_error("FindWindow");
        return false;
    }

    if (IsIconic(hwnd)) {
        callbackfw("WSA window is iconic. Try to show WSA window.");
        if (!ShowWindow(hwnd, SW_SHOW)) return false;
    }

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto wndStyle = GetWindowLong(hwnd, GWL_STYLE);
    auto wndExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    auto dpi = GetDpiForWindow(hwnd);

    RECT wantedSize = { .left = 0, .top = 0, .right = WindowWidthDefault, .bottom = WindowHeightDefault };
    RECT wantedClient = wantedSize;
    GetClientRect(hwnd, &wantedClient);
    RECT testCaption = { 0, 0, 100, 100 };
    AdjustWindowRectExForDpi(&testCaption, wndStyle, false, wndExStyle, dpi);

    m_caption_height = -testCaption.top;
    wantedSize.right = wantedClient.right;
    wantedSize.bottom = wantedClient.bottom - m_caption_height;
    m_arknights_roi = cv::Rect{ 0, m_caption_height, wantedSize.right, wantedSize.bottom };

    auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
        IGraphicsCaptureItemInterop>();
    result = factory->CreateForWindow(
        hwnd, winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(), winrt::put_abi(m_item));
    if (FAILED(result)) {
        fill_error("IGraphicsCaptureItem::CreateForWindow");
        return false;
    }

    auto dxgiDevice = m_native_device.as<IDXGIDevice>();
    result = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(),
        reinterpret_cast<IInspectable**>(winrt::put_abi(m_device)));
    if (FAILED(result)) {
        fill_error("CreateDirect3D11DeviceFromDXGIDevice");
        return false;
    }

    m_inputSize = m_item.Size();
    //m_cache.create(m_inputSize.Height, m_inputSize.Width, CV_8UC3);

    {
        D3D11_TEXTURE2D_DESC desc = { .Width = (UINT)m_inputSize.Width,
                                      .Height = (UINT)m_inputSize.Height,
                                      .MipLevels = 1,
                                      .ArraySize = 1,
                                      .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                                      .SampleDesc = {.Count = 1, .Quality = 0 },
                                      .Usage = D3D11_USAGE_STAGING,
                                      .CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE,
                                      .MiscFlags = 0 };
        result = m_native_device->CreateTexture2D(&desc, nullptr, m_staging_texture.put());
    }
    if (FAILED(result)) {
        fill_error("ID3D11Device::CreateTexture2D");
        return false;
    }

    m_frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        m_device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, m_inputSize);
    if (m_frame_pool == nullptr) {
        fill_error("winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded");
        return false;
    }

    return SUCCEEDED(result);
}

void WGCCapture::Release()
{
    LogTraceFunction;

    if (m_session) {
        m_locker->lock();
        m_session = nullptr;
        if (m_frame_arrived)
            m_frame_pool.FrameArrived(m_frame_arrived);
        m_locker->unlock();
        m_locker->lock();
        m_locker->unlock();
        m_locker = nullptr;
        m_recieved = nullptr;
    }
    m_frame_pool = nullptr;
    m_item = nullptr;
    m_device = nullptr;
    m_staging_texture = nullptr;
    m_context = nullptr;
    m_native_device = nullptr;
}

bool WGCCapture::StartSession()
{
    LogTraceFunction;

    if (EndSession()) return false;

    if (!IsWindow(m_hwnd)) {
        m_frame_pool = nullptr;
        m_session = nullptr;
        return false;
    }
    
    if (m_contiguous)
        m_frame_arrived = m_frame_pool.FrameArrived({ this, &WGCCapture::process_frame });

    m_session = m_frame_pool.CreateCaptureSession(m_item);
    m_session.IsBorderRequired(m_golden_border);
    m_session.IsCursorCaptureEnabled(false);
    m_session.StartCapture();

    return true;
}

bool WGCCapture::EndSession()
{
    LogTraceFunction;

    if (m_session) {
        m_locker->lock();
        m_session = nullptr;
        if (m_frame_arrived)
            m_frame_pool.FrameArrived(m_frame_arrived);
        m_locker->unlock();
        return true;
    }

    return false;
}

cv::Mat WGCCapture::Get(bool c3u8)
{
    if (m_contiguous)
    {
        m_locker->lock();
        if (!m_session)
        {
            if (m_locker) m_locker->unlock();
            return {};
        }

        while (!m_recieved->load())
            ;

        if (m_cache.empty()) [[unlikely]] {
            m_locker->unlock();
            return {};
            }
        cv::Mat payload = m_cache(m_arknights_roi).clone();
        m_recieved->store(false);
        m_locker->unlock();

        if (c3u8) cv::cvtColor(payload, payload, cv::COLOR_BGRA2BGR);

        return payload;
    }
    else
    {
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = nullptr;
        // Clear cache
        do { frame = m_frame_pool.TryGetNextFrame(); } while (frame != nullptr);
        // Up-to-date
        do { frame = m_frame_pool.TryGetNextFrame(); } while (frame != nullptr);

        CopyFromFrameCV(frame);

        cv::Mat payload = m_cache(m_arknights_roi).clone();
        if (c3u8) cv::cvtColor(payload, payload, cv::COLOR_BGRA2BGR);

        return payload;
    }
}

cv::Mat WGCCapture::Peek(bool c3u8)
{
    if (m_contiguous)
    {
        m_locker->lock();
        if (!m_session)
        {
            if (m_locker) m_locker->unlock();
            return {};
        }

        if (m_cache.empty()) [[unlikely]] {
            m_locker->unlock();
            return {};
        }
        cv::Mat payload = m_cache(m_arknights_roi).clone();
        m_locker->unlock();

        if (c3u8) cv::cvtColor(payload, payload, cv::COLOR_BGRA2BGR);

        return payload;
    }
    else
    {
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = nullptr;
        // Clear cache
        do { frame = m_frame_pool.TryGetNextFrame(); } while (frame != nullptr);
        // Up-to-date
        do { frame = m_frame_pool.TryGetNextFrame(); } while (frame != nullptr);

        CopyFromFrameCV(frame);

        cv::Mat payload = m_cache(m_arknights_roi).clone();
        if (c3u8) cv::cvtColor(payload, payload, cv::COLOR_BGRA2BGR);

        return payload;
    }
}

void WGCCapture::SetGoldenBorder(bool enable)
{
    m_golden_border = enable;
    if (EndSession()) StartSession();
}

void WGCCapture::SetContiguousGettingImgs(bool enable)
{
    m_contiguous = enable;
    if (EndSession()) StartSession();
}

void WGCCapture::CopyFromFrameCV(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame)
{
    winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> dxgiAccess = {
        frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()
    };
    winrt::com_ptr<::ID3D11Resource> resource;
    winrt::check_hresult(dxgiAccess->GetInterface(__uuidof(resource), resource.put_void()));

    m_context->CopyResource(m_staging_texture.get(), resource.get());

    D3D11_MAPPED_SUBRESOURCE field;
    UINT subresource = D3D11CalcSubresource(0, 0, 0);
    m_context->Map(m_staging_texture.get(), subresource, D3D11_MAP_READ, 0, &field);

    //const auto pitch = m_inputSize.Width * 3ull;
    //uchar* data = m_cache.ptr();
    //uchar* source = (uchar*)field.pData;
    //for (size_t i = 0; i < m_inputSize.Height; i++) {
    //    for (size_t j = 0; j < m_inputSize.Width; j++) {
    //        data[i * pitch + j * 3] = source[i * field.RowPitch + j * 4];
    //        data[i * pitch + j * 3 + 1] = source[i * field.RowPitch + j * 4 + 1];
    //        data[i * pitch + j * 3 + 2] = source[i * field.RowPitch + j * 4 + 2];
    //    }
    //}
    //m_recieved.store(true);
    //m_locker.unlock();
    m_cache = cv::Mat(m_inputSize.Height, m_inputSize.Width, CV_8UC4, field.pData, field.RowPitch);
    m_context->Unmap(m_staging_texture.get(), subresource);
}

void WGCCapture::CopyFromFrameCImage(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame, CImage& img)
{
    winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> dxgiAccess = {
        frame.Surface().as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>()
    };
    winrt::com_ptr<::ID3D11Resource> resource;
    winrt::check_hresult(dxgiAccess->GetInterface(__uuidof(resource), resource.put_void()));

    m_context->CopyResource(m_staging_texture.get(), resource.get());

    D3D11_MAPPED_SUBRESOURCE field;
    UINT subresource = D3D11CalcSubresource(0, 0, 0);
    m_context->Map(m_staging_texture.get(), subresource, D3D11_MAP_READ, 0, &field);

    memcpy(img.GetBits(), field.pData, img.GetHeight() * img.GetPitch());

    m_context->Unmap(m_staging_texture.get(), subresource);
}

void WGCCapture::process_frame(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& framePool, winrt::Windows::Foundation::IInspectable const& object)
{
    m_locker->lock();
    if (!m_session) {
        if (m_locker) m_locker->unlock();
        return;
    }

    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frame = framePool.TryGetNextFrame();
    auto size = frame.ContentSize();

    if (size != m_inputSize) {
        framePool.Recreate(
            m_device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, m_inputSize);
        m_locker->unlock();
        return;
    }

    CopyFromFrameCV(frame);

    m_recieved->store(true);
    m_locker->unlock();
}