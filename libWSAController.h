#pragma once

#if defined(LIBWSACONTROLLER_EXPORT)
#define LIBWSACONTROLLER __declspec(dllexport)
#else
#define LIBWSACONTROLLER __declspec(dllimport)
#endif

namespace libWSAController
{
	// void callback(sender, level, msg);
	using lib_callback = std::function<void(std::string_view, std::string_view, std::string_view)>;

	class IWSAController
	{
	public:
		virtual bool Init(bool resizeWindow, bool goldenBorder, bool replace, lib_callback) PURE;
		virtual void ReleaseAll() PURE;

		virtual void AddPackageName(tstring pkgName) PURE; // Deprecated
		virtual std::vector<tstring> GetSelectedPackageName() PURE; // Deprecated

		virtual bool StartCapture(tstring pkgName) PURE;
		virtual cv::Mat GetOnce(bool c3u8, tstring pkgName) PURE;
		virtual cv::Mat PeekOnce(bool c3u8, tstring pkgName) PURE;
		virtual bool EndCapture(tstring pkgName) PURE;

		virtual bool InputInterrupt(tstring pkgName) PURE;

		virtual bool MouseLeftClick(int x, int y, tstring pkgName) PURE;
		virtual bool MouseLeftSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName) PURE;

		virtual bool TouchClick(int x, int y, tstring pkgName) PURE;
		virtual bool TouchSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName) PURE;

		virtual bool MouseLeftDown(int x, int y, tstring pkgName) PURE;
		virtual bool MouseMove(int x, int y, tstring pkgName) PURE;
		virtual bool MouseLeftUp(int x, int y, tstring pkgName) PURE;
		virtual UINT64 TouchDown(int x, int y, tstring pkgName) PURE;
		virtual bool TouchMove(int x, int y, UINT64 id, tstring pkgName) PURE;
		virtual bool TouchUp(int x, int y, UINT64 id, tstring pkgName) PURE;
		virtual size_t CountTouchedPoints(tstring pkgName) PURE;

		virtual void WaitInput(tstring pkgName) PURE;
	};

	void LIBWSACONTROLLER CreateWSAController(IWSAController**);

	class WSAControllerWrap
	{
	public:
		static WSAControllerWrap LIBWSACONTROLLER CreateWSAController(lib_callback, bool resizeWindow, bool goldenBorder, bool replace);

		explicit WSAControllerWrap(std::nullptr_t = nullptr) : m_(nullptr) {}
		explicit WSAControllerWrap(IWSAController* ptr) : m_(ptr) {}
		explicit WSAControllerWrap(WSAControllerWrap&& o) : m_(o.m_) { o.m_ = nullptr; }

		WSAControllerWrap& operator=(const WSAControllerWrap&) = delete;
		WSAControllerWrap& operator=(const WSAControllerWrap&& o) { m_ = o.m_; }

		operator bool()
		{
			return (m_ != nullptr);
		}

	public:
		bool StartCapture(tstring pkgName = {}) { return m_->StartCapture(pkgName); }
		cv::Mat GetOnce(bool c3u8 = true, tstring pkgName = {}) { return m_->GetOnce(c3u8, pkgName); }
		cv::Mat PeekOnce(bool c3u8 = true, tstring pkgName = {}) { return m_->PeekOnce(c3u8, pkgName); }
		bool EndCapture(tstring pkgName = {}) { return m_->EndCapture(pkgName); }

		bool MouseLeftClick(int x, int y, tstring pkgName = {}) { return m_->MouseLeftClick(x, y, pkgName); }
		bool MouseLeftSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName = {})
			{ return m_->MouseLeftSwipe(sx, sy, ex, ey, dur, slow_end, pkgName); }

		bool TouchClick(int x, int y, tstring pkgName = {}) { return m_->TouchClick(x, y, pkgName); }
		bool TouchSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName = {})
			{ return m_->TouchSwipe(sx, sy, ex, ey, dur, slow_end, pkgName); }

		void WaitInput(tstring pkgName = {}) { m_->WaitInput(pkgName); }

	private:
		IWSAController* m_;

	};
}