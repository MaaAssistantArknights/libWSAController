#pragma once

class WGCCapture
{
public:
	~WGCCapture() { Release(); }

	bool Attach(HWND);
	void Release();

	bool StartSession();
	bool EndSession();

	cv::Mat Get(bool c3u8);
	cv::Mat Peek(bool c3u8);

	void SetGoldenBorder(bool enable);
	void SetContiguousGettingImgs(bool enable);

private:
	void CopyFromFrameCV(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame);
	void CopyFromFrameCImage(
		winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame, CImage& img
	);

	void process_frame(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& framePool,
		[[maybe_unused]] winrt::Windows::Foundation::IInspectable const& object);

private:
	bool m_golden_border = false;
	bool m_contiguous = true;

	HWND m_hwnd = NULL;
	int m_caption_height = 45;
	winrt::Windows::Graphics::SizeInt32 m_inputSize;

	cv::Mat m_cache;
	cv::Rect m_arknights_roi;
	std::unique_ptr<std::atomic_bool> m_recieved;
	std::unique_ptr <std::mutex> m_locker;
	winrt::event_token m_frame_arrived = { 0 };

	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_frame_pool = nullptr;
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session = nullptr;
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item = nullptr;

	winrt::com_ptr<ID3D11Device> m_native_device = nullptr;
	winrt::com_ptr<ID3D11DeviceContext> m_context = nullptr;
	winrt::com_ptr<ID3D11Texture2D> m_staging_texture = nullptr;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device = nullptr;
};