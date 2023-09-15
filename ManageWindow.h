#pragma once

namespace SuperWindow
{
	using WindowID = uint64_t;

	bool InitWith(libWSAController::IWSAController*);
	std::optional<WindowID> Monitor(HWND, tstring pkgName, tstring title, bool block = false);
	bool MoveAway(WindowID);
	bool MoveBack(WindowID);
	void Redirect(WindowID);
	void ReleaseAll();
}