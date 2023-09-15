#pragma once

namespace SuperToucher
{
    bool IsReady();

    bool Attach(DWORD processId);
    void Release();

    std::optional<TouchedPointID> Down(HWND, int x, int y, bool closed = true);
    bool Update(TouchedPointID, int x, int y, bool closed = true);
    bool Up(TouchedPointID, int x, int y, bool closed = true);
    bool UpAll();

    void WaitTouch();

    bool MouseLeftDown();
    bool MouseLeftUp();
};