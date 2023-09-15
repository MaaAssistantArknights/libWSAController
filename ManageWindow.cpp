#include "pch.h"

#define LIBWSACONTROLLER_USEATLIMAGE
#include "libWSAController.h"
#include "ManageWindow.h"
#include "CallbackLogger.h"
#include "VirtualTouch.h"

namespace SuperWindow
{
	struct WndInfo
	{
		HWND wsa, redir;
		RECT init_pos;
		tstring pkgName, title;
		bool blocked;
		clock_t last_time;
	};
	
	std::jthread m_mainLoop;
	static std::map<WindowID, WndInfo> m_allWnds;
	static std::map<HWND, WindowID> m_red2init;
	std::map<WORD, UINT64> m_pid2tid;
	static size_t windowIDPool = 0;
	static HINSTANCE hInstance = GetModuleHandle(NULL);
	inline WindowID MakeWindowID() { return windowIDPool++; }
	static int x_start = SHRT_MIN, moved_count = 0;
	libWSAController::IWSAController* ictrl;
	// 假设WSA窗口总大小较小
	/*
	static class PlanningModule
	{
	private:
		struct {
			int top, right, bottom, left;
		} m_thickness = {};
		struct {
			int top, right, bottom, left;
		} m_space = { 65536, 65536, 65536, 65536 };
		struct {
			int top, right, bottom, left;
		} m_start = {};
		struct {
			std::vector<std::pair<bool, int>> top, right, bottom, left;
		} m_used;
		
	public:
		bool Emplace(WindowID id)
		{
			const RECT& pos = m_allWnds[id].init_pos;
			int height = pos.bottom - pos.top, width = pos.right - pos.left;
			if (height <= m_thickness.top)
			{
				if (width <= m_space.top)
				{
					SetWindowPos(m_allWnds[id].wsa, NULL, m_start.top, 0, width, height, SWP_NOZORDER);
					m_space.top -= width, m_start.top += width;
					m_used.top.push_back(std::make_pair(true, width));
					return true;
				}
				for (auto& i : m_used.top)
				{

				}
			}
		}

	} m_planning;
	*/

	LRESULT CALLBACK RedirectedWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		WndInfo* pInfo = (WndInfo*)GetWindowLongPtr(hWnd, -21);
		if (message == WM_DESTROY)
		{
			MoveBack(m_red2init[hWnd]);
			PostQuitMessage(0);
		}
		if (pInfo == nullptr)
			return DefWindowProc(hWnd, message, wParam, lParam);
		if (message >= 0x200 && message < 0x2a3 && pInfo->blocked) return 0;
		POINTS pts = MAKEPOINTS(lParam);
		switch (message)
		{
		case WM_MOUSEMOVE:
		{
			if (GetKeyState(VK_LBUTTON) & 0x8000)
			{
				ictrl->MouseMove(pts.x, pts.y, pInfo->pkgName);
				callbackfd("MouseMove");
			}
		}
		break;
		case WM_LBUTTONDOWN:
		{
			ictrl->MouseLeftDown(pts.x, pts.y, pInfo->pkgName);
			callbackfd("MouseLeftDown");
		}
		break;
		case WM_LBUTTONUP:
		{
			ictrl->MouseLeftUp(pts.x, pts.y, pInfo->pkgName);
			callbackfd("MouseLeftUp");
		}
		break;
		case WM_POINTERDOWN:
		{
			UINT64 tid = ictrl->TouchDown(pts.x, pts.y, pInfo->pkgName);
			m_pid2tid[GET_POINTERID_WPARAM(wParam)] = tid;
			callbackfd("PointerDown");
		}
		break;
		case WM_POINTERUPDATE:
		{
			ictrl->TouchMove(pts.x, pts.y, m_pid2tid[GET_POINTERID_WPARAM(wParam)], pInfo->pkgName);
			callbackfd("PointerUpdate");
		}
		break;
		case WM_POINTERUP:
		{
			ictrl->TouchUp(pts.x, pts.y, m_pid2tid[GET_POINTERID_WPARAM(wParam)], pInfo->pkgName);
			m_pid2tid.erase(GET_POINTERID_WPARAM(wParam));
			callbackfd("PointerUp");
		}
		break;
		case WM_MOUSELEAVE:
		case WM_POINTERLEAVE:
		{
			ictrl->InputInterrupt(pInfo->pkgName);
			callbackfd("Interrupted input!!!!!!!");
		}
		break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	}

	static struct {
		bool need = false;
		WindowID dst = 0;
	} m_pipe;
	bool InitWith(libWSAController::IWSAController* c) {
		ictrl = c;
		auto loopFunc = [](std::stop_token token) {
			std::set<WindowID> allReds;
			MSG msg{};
			while (!token.stop_requested()) {
				if (m_pipe.need)
				{
					WndInfo& info = m_allWnds[m_pipe.dst];
					WNDCLASS cls = {
						.style = CS_HREDRAW | CS_VREDRAW,
						.lpfnWndProc = RedirectedWndProc,
						.cbClsExtra = 0,
						.cbWndExtra = sizeof(LPVOID),
						.hInstance = hInstance,
						.hIcon = NULL,
#pragma warning(disable:4302)
						.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)),
#pragma warning(default:4302)
						.hbrBackground = (HBRUSH)(COLOR_BACKGROUND + 1),
						.lpszMenuName = NULL,
						.lpszClassName = info.pkgName.c_str()
					};
					RegisterClass(&cls);
					HWND hRed = CreateWindow(info.pkgName.c_str(), info.title.c_str(),
						GetWindowLong(info.wsa, GWL_STYLE), 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
					SetWindowLongPtr(hRed, -21, (UINT64)&info);
					auto x = info.init_pos.left, y = info.init_pos.top;
					auto width = info.init_pos.right - x, height = info.init_pos.bottom - y;
					x = x < 0 ? 0 : x; x = x > GetSystemMetrics(SM_CXSCREEN) - 10 ? 0 : x;
					y = y < 0 ? 0 : y; y = y > GetSystemMetrics(SM_CYSCREEN) - 10 ? 0 : y;
					SetWindowPos(hRed, NULL, x, y, width, height, SWP_NOZORDER);
					ShowWindow(hRed, SW_SHOW);
					UpdateWindow(hRed);
					info.redir = hRed;
					m_red2init[hRed] = m_pipe.dst;
					allReds.insert(m_pipe.dst);
					m_pipe.need = false;
					continue;
				}
				// callbackfd(std::format("Try to invalid windows, total: {}", allReds.size()))
				for (auto& i : allReds)
				{
					BOOL got_msg = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
					if (got_msg) {
						TranslateMessage(&msg);
						DispatchMessage(&msg);
					}
					auto& info = m_allWnds[i];
					cv::Mat img = ictrl->PeekOnce(false, info.pkgName);
					if (img.empty()) continue;
					HBITMAP bmp = CreateBitmap(img.cols, img.rows, 1, 32, img.data);
					auto hdc = GetDC(info.redir);
					auto hdcBits = CreateCompatibleDC(hdc);
					SelectObject(hdcBits, bmp);
					BitBlt(hdc, 0, 0, img.cols, img.rows, hdcBits, 0, 0, SRCCOPY);
					DeleteDC(hdcBits);
					ReleaseDC(info.redir, hdc);
					DeleteObject(bmp);
				}
				std::this_thread::yield();
			}
			for (auto& i : m_allWnds)
			{
				if (!DestroyWindow(i.second.redir))
					callbackfe(std::format("DestroyWindow Failed!! GetLastError() = {}", GetLastError()));
			}
			while (GetMessage(&msg, NULL, 0, 0)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		};
		m_mainLoop = std::jthread(loopFunc);
		return m_mainLoop.joinable();
	}

	std::optional<WindowID> Monitor(HWND hwnd, tstring pkgName, tstring title, bool block)
	{
		auto result = MakeWindowID();
		RECT wnd_rect; GetWindowRect(hwnd, &wnd_rect);
		m_allWnds[result] = WndInfo{ hwnd, NULL, wnd_rect, pkgName, title, block, 0 };
		return result;
	}
	bool MoveAway(WindowID id)
	{
		if (m_allWnds.find(id) == m_allWnds.cend()) {
			callbackfe("Not found window id");
			return false;
		}
		if (x_start >= SHRT_MAX - 1) {
			callbackfe("Not enough space to move window!");
			return false;
		}
		WndInfo& info = m_allWnds[id];
		auto width = info.init_pos.right - info.init_pos.left,
			height = info.init_pos.bottom - info.init_pos.top;
		SetWindowPos(info.wsa, NULL, x_start, 0, width, height, SWP_NOZORDER);
		x_start += width, moved_count++;
		return true;
	}
	bool MoveBack(WindowID id)
	{
		if (m_allWnds.find(id) == m_allWnds.cend()) {
			callbackfe("Not found window id");
			return false;
		}
		WndInfo& info = m_allWnds[id];
		auto width = info.init_pos.right - info.init_pos.left,
			height = info.init_pos.bottom - info.init_pos.top;
		SetWindowPos(info.wsa, NULL, info.init_pos.left, info.init_pos.top, width, height, SWP_NOZORDER);
		if (id == windowIDPool - 1) x_start -= width;
		if (--moved_count == 0) x_start = 0;
		return true;
	}
	void Redirect(WindowID id)
	{
		if (m_allWnds.find(id) == m_allWnds.cend()) return;
		while (m_pipe.need);
		m_pipe.dst = id, m_pipe.need = true;
	}
	void ReleaseAll()
	{
		m_mainLoop.request_stop();
		if (m_mainLoop.joinable()) m_mainLoop.join();
	}
}
