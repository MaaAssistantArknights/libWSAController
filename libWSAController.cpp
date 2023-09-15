#include "pch.h"

#define LIBWSACONTROLLER_EXPORT
#define LIBWSACONTROLLER_USEATLIMAGE
#include "libWSAController.h"

#include "CallbackLogger.h"
#include "Capture.h"
#include "Inputs.h"
#include "ManageWindow.h"

using namespace libWSAController;

class WSAController : public IWSAController
{
public:
	WSAController& operator=(const WSAController&) = delete;
	WSAController& operator=(WSAController&&) = delete;

private:
	DWORD m_processId = 0;

	struct WSAWindow
	{
		tstring title, name;
		HWND hwnd = NULL;
		inline bool operator==(const WSAWindow& b) const
		{
			return title == b.title && name == b.name && hwnd == b.hwnd;
		}
	};
	std::vector<WSAWindow> m_windows;
	std::vector<std::unique_ptr<WGCCapture>> m_captures;
	std::vector<std::unique_ptr<InputInjector>> m_inputs;
	std::vector<SuperWindow::WindowID> m_wnds;

private:
	inline auto GetToolPosFromPkg(tstring pkgName)
	{
		size_t target_pos = m_windows.size() - 1;
		if (!pkgName.empty())
		{
			auto found_iter = std::find_if(m_windows.cbegin(),
				m_windows.cend(),
					[&pkgName](const WSAWindow& info) {
						return pkgName == info.name;
					});
			if (found_iter == m_windows.cend())
				return (size_t)-1;
			target_pos = std::distance(m_windows.cbegin(), found_iter);
		}
		return target_pos;
	}

public:
	bool Init(bool resizeWindow, bool goldenBorder, bool replace, lib_callback callback_function)
	{
		ReleaseAll();

		if (callback_function == nullptr)
			return false;
		SetCallbackFunction(callback_function);

		SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

		if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
			callback("", "ERR", "PlatformNotSupported");
			return false;
		}

		// Get debug privilege
		{
			HANDLE token;
			LUID se_debugname;

			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
				return false;
			}
			if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &se_debugname)) {
				CloseHandle(token);
				return false;
			}
			TOKEN_PRIVILEGES tkp;
			tkp.PrivilegeCount = 1;
			tkp.Privileges[0].Luid = se_debugname;
			tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			if (!AdjustTokenPrivileges(token, FALSE, &tkp, sizeof(tkp), NULL, NULL))
			{
				CloseHandle(token);
				return false;
			}
		}

		// Scan for WSA Process
		{
			DWORD aProcesses[1024], cbNeeded, cProcesses;
			TCHAR szProcessName[1024] = L"";
			// Get the list of process identifiers.
			if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
			{
				callbackfe(std::format("Cannot list processes! GetLastError()={}", GetLastError()));
				return false;
			}

			// Calculate how many process identifiers were returned.
			cProcesses = cbNeeded / sizeof(DWORD);
			// Print the name and process identifier for each process.
			for (size_t i = 0; i < cProcesses; i++)
			{
				if (aProcesses[i] != 0)
				{
					// Get a handle to the process.
					HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
						PROCESS_VM_READ,
						FALSE, aProcesses[i]);

					// Get the process name.
					if (NULL != hProcess)
					{
						HMODULE hMod;
						DWORD cbNeeded;

						if (EnumProcessModules(hProcess, &hMod, sizeof(hMod),
							&cbNeeded))
						{

							GetModuleBaseName(hProcess, hMod, szProcessName,
								sizeof(szProcessName) / sizeof(TCHAR));
							if (StrCmp(szProcessName, TEXT("WsaClient.exe")) == 0)
							{
								m_processId = aProcesses[i];
								CloseHandle(hProcess);
								break;
							}
						}

						CloseHandle(hProcess);
						hProcess = NULL;
					}
					// Release the handle to the process.
				}
			}
		}

		if (m_processId == 0)
		{
			callbackfe("No WSA Process!!!");
			return false;
		}

		// Scan for WSA Windows
		{
			auto enumWndProc = [](HWND hwnd, LPARAM p) -> BOOL {
				struct
				{
					DWORD id;
					std::vector<WSAWindow>* pvector;
				} *lparam = decltype(lparam)(p);
				DWORD cur_id = 0;
				if (GetWindowThreadProcessId(hwnd, &cur_id);
					cur_id == lparam->id)
				{
					TCHAR title[1024], name[1024];
					GetWindowText(hwnd, title, 1024);
					GetClassName(hwnd, name, 1024);
					lparam->pvector->push_back({ title, name, hwnd });
				}
				return TRUE;
			};
			struct
			{
				DWORD id;
				std::vector<WSAWindow>* phwnds;
			} lparam{ m_processId, &m_windows };
			EnumWindows(enumWndProc, LPARAM(&lparam));
			std::erase_if(m_windows, [](WSAWindow& wnd) {
				DWORD style = GetWindowLong(wnd.hwnd, GWL_STYLE);
				constexpr auto dst_stye = WS_CAPTION | WS_VISIBLE;
				if ((style & dst_stye) == dst_stye)
					return false;
				return true;
			});
			if (replace) SuperWindow::InitWith(this);
		}
		
		// Attach target WSA Window !!!!!!!
		{
			for (auto& target : m_windows)
			{
				if (resizeWindow)
				{
					RECT wantedWindow = { .left = 0, .top = 0, .right = WindowWidthDefault, .bottom = WindowHeightDefault };
					auto wndStyle = GetWindowLong(target.hwnd, GWL_STYLE);
					auto wndExStyle = GetWindowLong(target.hwnd, GWL_EXSTYLE);
					auto dpi = GetDpiForWindow(target.hwnd);
					AdjustWindowRectExForDpi(&wantedWindow, wndStyle, false, wndExStyle, dpi);
					wantedWindow.right -= wantedWindow.left;
					wantedWindow.bottom -= wantedWindow.top;
					wantedWindow.top = wantedWindow.left = 0;
					SetWindowPos(target.hwnd, NULL, 0, 0, wantedWindow.right, wantedWindow.bottom, SWP_NOMOVE);
				}

				WGCCapture* capture = new WGCCapture;
				InputInjector* inputs = new InputInjector;
				if (!capture->Attach(target.hwnd)) return false;
				if (!inputs->Attach(target.hwnd, m_processId)) return false;

				m_captures.emplace_back(capture);
				m_inputs.emplace_back(inputs);
			}
			if (replace)
			{
				for (auto& i : m_windows)
				{
					StartCapture(i.name);
					auto id = SuperWindow::Monitor(i.hwnd, i.name, i.title);
					if (!id) {
						callbackfe("Monitor window failed!");
						ReleaseAll();
						return false;
					}
					m_wnds.emplace_back(*id);
					SuperWindow::MoveAway(*id);
					SuperWindow::Redirect(*id);
				}
			}
		}

		return true;
	}
	void ReleaseAll()
	{
		SuperWindow::ReleaseAll();
		m_captures.clear();
		m_inputs.clear();
		m_windows.clear();
		SetCallbackFunction(nullptr);
		m_processId = 0;
	}

	void AddPackageName(tstring pkgName)
	{

	}
	std::vector<tstring> GetSelectedPackageName()
	{
		return {};
	}

	bool StartCapture(tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_captures[target_pos]->StartSession();
	}
	cv::Mat GetOnce(bool c3u8, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return {};
		return m_captures[target_pos]->Get(c3u8);
	}
	cv::Mat PeekOnce(bool c3u8, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return {};
		return m_captures[target_pos]->Peek(c3u8);
	}
	bool EndCapture(tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_captures[target_pos]->EndSession();
	}

	bool InputInterrupt(tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->Interrupt();
	}

	bool MouseLeftClick(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->MouseLeftClick(x, y);
	}
	bool MouseLeftSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		if (slow_end)
			return m_inputs[target_pos]->MouseLeftSwipe(sx, sy, ex, ey, dur);
		else
			return m_inputs[target_pos]->MouseLeftSwipePrecisely(sx, sy, ex, ey, dur);
	}

	bool TouchClick(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->TouchClick(x, y);
	}
	bool TouchSwipe(int sx, int sy, int ex, int ey, int dur, int slow_end, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		if (slow_end)
			return m_inputs[target_pos]->TouchSwipe(sx, sy, ex, ey, dur);
		else
			return m_inputs[target_pos]->TouchSwipePrecisely(sx, sy, ex, ey, dur);
	}

	bool MouseLeftDown(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->MouseLeftDown(x, y);
	}
	bool MouseMove(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->MouseMove(x, y);
	}
	bool MouseLeftUp(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->MouseLeftUp(x, y);
	}
	UINT64 TouchDown(int x, int y, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->TouchDown(x, y);
	}
	bool TouchMove(int x, int y, UINT64 id, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->TouchMove(x, y, id);
	}
	bool TouchUp(int x, int y, UINT64 id, tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->TouchUp(x, y, id);
	}

	size_t CountTouchedPoints(tstring pkgName)
	{
		auto target_pos = GetToolPosFromPkg(pkgName);
		if (target_pos == (size_t)-1) return false;
		return m_inputs[target_pos]->CountTouchedPoints();
	}

	void WaitInput(tstring pkgName)
	{
		for (auto& i : m_inputs) i->Wait();
	}
};

void LIBWSACONTROLLER libWSAController::CreateWSAController(IWSAController** pp)
{
	if (pp == nullptr) return;
	*pp = new WSAController;
}

WSAControllerWrap LIBWSACONTROLLER libWSAController::WSAControllerWrap::CreateWSAController(
	lib_callback callback_function, bool resizeWindow, bool goldenBorder, bool replace
)
{
	auto pInstance = new WSAController();
	pInstance->Init(resizeWindow, goldenBorder, replace, callback_function);
	return WSAControllerWrap(pInstance);
}