#include "pch.h"

#include "libWSAController.h"
#include "Inputs.h"
#include "VirtualTouch.h"
#include "CallbackLogger.h"

static size_t inner_id = 0;
static size_t m_count = 0;
constexpr size_t max_retry_times = 2048;

enum InputType
{
    KeyPress = -1,
    TimerStart = 100,
    TimerCheck = 200,
    LeftClick = 0,
    LeftDown = 1,
    LeftUp = 2,
    MouseMove = 3,
    MouseMoveWithKeyPressed = 4,
    TouchClick = 5,
    TouchDown = 6,
    TouchMove = 7,
    TouchUp = 8,
};

#define msg_left_click(x,y) m_msgs.push(MsgUnit{ 0, 0, MAKELPARAM(x, y + m_caption_height) })
#define msg_left_down(x,y) m_msgs.push(MsgUnit{ 1, 0, MAKELPARAM(x, y + m_caption_height) })
#define msg_left_up(x,y) m_msgs.push(MsgUnit{ 2, 0, MAKELPARAM(x, y + m_caption_height) })
#define msg_left_hover(x,y) m_msgs.push(MsgUnit{ 3, 0, MAKELPARAM(x, y + m_caption_height) })
#define msg_left_hover_key(x,y,wparam) m_msgs.push(MsgUnit{ 4, wparam, MAKELPARAM(x, y + m_caption_height) })
#define msg_tic() m_msgs.push(MsgUnit{ 100, 0, 0 })
#define msg_toc_wait(time_stamp) m_msgs.push({ 200, (WPARAM)time_stamp, 0 })
#define msg_touch_click(x,y) m_msgs.push(MsgUnit{ 5, 0, MAKELPARAM(x, y + m_caption_height) })
#define msg_touch_down(x,y,new_id) m_msgs.push(MsgUnit{ 6, new_id = inner_id++, MAKELPARAM(x, y + m_caption_height) })
#define msg_touch_update(x,y,id) m_msgs.push(MsgUnit{ 7, id, MAKELPARAM(x, y + m_caption_height) })
#define msg_touch_up(x,y,id) m_msgs.push(MsgUnit{ 8, id, MAKELPARAM(x, y + m_caption_height) })

inline double InputInjector::linear_interpolate(double& sx, double& sy, double dx, double dy,
    double time_stamp, double dur_slice, double n, UINT64 touched)
{
    for (int i = 1; i <= n; i++) {
        sx += dx, sy += dy;
        msg_toc_wait(time_stamp);
        if (touched == UINT64_MAX) msg_left_hover(sx, sy);
        else msg_touch_update(sx, sy, touched);
        time_stamp += dur_slice;
    }
    return time_stamp;
}

struct RunInputInjectorMainEssential
{
    std::queue<InputInjector::MsgUnit>* pMsgs;
    HWND hWnd;
};

std::mutex write_lock;
void RunInputInjectorMain(InputInjector* self)
{
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    using namespace std::chrono;
    time_point<high_resolution_clock, milliseconds> tic;
    time_point<high_resolution_clock, milliseconds> toc;
    std::optional<TouchedPointID> m_touch;
    uint64_t retry_times = 0;

    callback(std::format("InputInjector({:#16x})", UINT64(self)), "INF", "Create thread!");
    while (!self->m_inited && retry_times < max_retry_times) {
        retry_times++;
        std::this_thread::yield();
    }
    if (retry_times == max_retry_times) {
        callback(std::format("InputInjector({:#16x})", UINT64(self)), "ERR", "Failed to run thread!!");
        self->stop_requested = true;
        return;
    }
    retry_times = 0;
    self->stop_possible = true;
    HWND m_hwnd = self->m_hwnd;
    auto& m_msgs = self->m_msgs;

    callback(std::format("InputInjector({:#16x})", UINT64(self)), "INF", "Thread Succeeded!!");

    while (!self->stop_requested) {
        if (self->Empty()) {
            std::this_thread::yield();
            continue;
        }

        InputInjector::MsgUnit& command = m_msgs.front();

        write_lock.lock();
        switch (command.type) {
        case InputType::KeyPress:
            SendMessage(m_hwnd, WM_KEYDOWN, command.wParam, 0);
            Sleep(80);
            SendMessage(m_hwnd, WM_KEYUP, command.wParam, 0);
            Sleep(80);
            break;
        case InputType::LeftClick:
            if (!SuperToucher::MouseLeftDown())
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_KEYDOWN, VK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_LBUTTONDOWN, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            Sleep(80);
            if (!SuperToucher::MouseLeftUp())
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_LBUTTONUP, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_KEYUP, VK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSELEAVE, 0, 0);
            Sleep(80);
            break;
        case InputType::TimerStart:
            tic = time_point_cast<milliseconds>(high_resolution_clock::now());
            break;
        case InputType::TimerCheck:
            toc = time_point_cast<milliseconds>(high_resolution_clock::now());
            while (toc - tic <= milliseconds{ command.wParam }) {
                toc = time_point_cast<milliseconds>(high_resolution_clock::now());
            }
            break;
        case InputType::LeftDown:
            if (!SuperToucher::MouseLeftDown())
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_KEYDOWN, VK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_LBUTTONDOWN, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            break;
        case InputType::LeftUp:
            if (!SuperToucher::MouseLeftUp())
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_LBUTTONUP, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_KEYUP, VK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEHOVER, 0, command.lParam);
            SendMessage(m_hwnd, WM_MOUSELEAVE, 0, 0);
            Sleep(80);
            break;
        case InputType::MouseMove:
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEMOVE, MK_LBUTTON, command.lParam);
            break;
        case InputType::MouseMoveWithKeyPressed:
            SendMessage(m_hwnd, WM_MOUSEHOVER, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_MOUSEMOVE, MK_LBUTTON, command.lParam);
            SendMessage(m_hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessage(m_hwnd, WM_KEYUP, VK_ESCAPE, 0);
            break;
        }

        POINTS touch_pt_s = MAKEPOINTS(command.lParam);
        POINT touch_pt = { touch_pt_s.x, touch_pt_s.y };
        if (self->HasTouch(command.wParam))
            m_touch = self->m_touchids[command.wParam];
        switch (command.type)
        {
        case InputType::TouchClick:
            ClientToScreen(m_hwnd, &touch_pt);
            m_touch = SuperToucher::Down(m_hwnd, touch_pt.x, touch_pt.y);
            if (!m_touch)
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            Sleep(80);
            SuperToucher::Up(*m_touch, touch_pt.x, touch_pt.y);
            Sleep(20);
            break;
        case InputType::TouchDown:
            ClientToScreen(m_hwnd, &touch_pt);
            m_touch = SuperToucher::Down(m_hwnd, touch_pt.x, touch_pt.y);
            if (!m_touch)
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            self->m_touchids[command.wParam] = *m_touch;
            break;
        case InputType::TouchMove:
            ClientToScreen(m_hwnd, &touch_pt);
            if (!SuperToucher::Update(*m_touch, touch_pt.x, touch_pt.y))
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            break;
        case InputType::TouchUp:
            ClientToScreen(m_hwnd, &touch_pt);
            if (!SuperToucher::Up(*m_touch, touch_pt.x, touch_pt.y))
            {
                if (retry_times > max_retry_times)
                    throw std::runtime_error("touch err!");
                retry_times++;
                write_lock.unlock();
                std::this_thread::yield();
                continue;
            }
            self->m_touchids.erase(*m_touch);
            break;
        }
        write_lock.unlock();

        m_msgs.pop();
        retry_times = 0;
    }
}

bool InputInjector::Attach(HWND hwnd, DWORD processId)
{
    Release();
    m_hwnd = hwnd;

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto wndStyle = GetWindowLong(hwnd, GWL_STYLE);
    auto wndExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    auto dpi = GetDpiForWindow(hwnd);
    RECT wantedSize = { .left = 0, .top = 0, .right = WindowWidthDefault, .bottom = WindowHeightDefault };
    RECT wantedWindow = wantedSize, wantedClient = wantedSize;
    GetClientRect(hwnd, &wantedClient);
    GetWindowRect(hwnd, &wantedWindow);
    wantedWindow.bottom -= wantedWindow.top;
    wantedWindow.right -= wantedWindow.left;
    wantedWindow.left = wantedWindow.top = 0;
    RECT testCaption = { 0, 0, 100, 100 };
    AdjustWindowRectExForDpi(&testCaption, wndStyle, false, wndExStyle, dpi);
    auto black = wantedWindow.bottom - wantedClient.bottom;
    int caption_height = testCaption.bottom - testCaption.top - 100 - black;

    std::mt19937_64 random_engine{ std::random_device {}() };
    std::uniform_real_distribution<double> dist(-5, 5);

    if (!SuperToucher::IsReady() && !SuperToucher::Attach(processId)) return false;

    UgencyClear();

    m_inited = true;
    while (!stop_possible) std::this_thread::yield();
    if (stop_requested) {
        m_inited = false;
        callbackfe("Cannot create touch thread!!");
        return false;
    }
    m_count++;
    return true;
}

void InputInjector::Release()
{
    StopThread();
    while (!Empty()) m_msgs.pop();
    if (m_inited) m_count--;
    if (m_count == 0)
        SuperToucher::Release();
}

bool InputInjector::MouseLeftClick(int x, int y)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    msg_left_click(x, y);

    return true;
}

bool InputInjector::MouseLeftSwipe(double sx, double sy, double ex, double ey, double dur)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    constexpr int nslices = 32;
    auto dx = (ex - sx) / nslices, dy = (ey - sy) / nslices;
    auto dur_slice = dur / nslices;

    msg_left_down(sx, sy);
    msg_tic();

    linear_interpolate(sx, sy, dx, dy, 0, dur_slice, nslices);

    msg_left_up(sx, sy);

    return true;
}

bool InputInjector::MouseLeftSwipePrecisely(double sx, double sy, double ex, double ey, double dur)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    constexpr int nslices = 16;
    constexpr int neslices = 32;
    constexpr int extra_slice_size = 3;
    constexpr double time_ratio = 3 / 8.;
    ex += extra_slice_size, ey += extra_slice_size;

    auto dx = (ex - sx) / nslices, dy = (ey - sy) / nslices;
    auto ux = std::abs(dx) / dx, uy = std::abs(dy) / dy;

    auto dur_slice = dur * time_ratio / nslices;

    msg_left_down(sx, sy);
    msg_tic();

    auto time_stamp = linear_interpolate(sx, sy, dx, dy, 0, dur_slice, nslices);

    dx = -ux * extra_slice_size / neslices, dy = -uy * extra_slice_size / neslices;
    time_stamp = linear_interpolate(sx, sy, dx, dy, time_stamp, dur_slice, neslices);
    linear_interpolate(sx, sy, 0, 0, time_stamp, 0, 10);

    msg_left_up(sx, sy);

    return true;
}

void InputInjector::Wait()
{
    while (!Empty());
    SuperToucher::WaitTouch();
}

bool InputInjector::TouchClick(int x, int y)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    msg_touch_click(x, y);

    return true;
}

bool InputInjector::TouchSwipe(double sx, double sy, double ex, double ey, double dur)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    constexpr int nslices = 16;
    auto dx = (ex - sx) / nslices, dy = (ey - sy) / nslices;
    auto dur_slice = dur / nslices;

    UINT64 touched = UINT64_MAX;
    msg_touch_down(sx, sy, touched);
    msg_tic();

    linear_interpolate(sx, sy, dx, dy, 0, dur_slice, nslices, touched);

    msg_touch_up(sx, sy, touched);

    return true;
}

bool InputInjector::TouchSwipePrecisely(double sx, double sy, double ex, double ey, double dur)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;

    constexpr int nslices = 8;
    constexpr int neslices = 16;
    constexpr int extra_slice_size = 3;
    constexpr double time_ratio = 3 / 8.;
    ex += extra_slice_size, ey += extra_slice_size;

    auto dx = (ex - sx) / nslices, dy = (ey - sy) / nslices;
    auto ux = std::abs(dx) / dx, uy = std::abs(dy) / dy;

    auto dur_slice = dur * time_ratio / nslices;

    UINT64 touched = UINT64_MAX;
    msg_touch_down(sx, sy, touched);
    msg_tic();

    auto time_stamp = linear_interpolate(sx, sy, dx, dy, 0, dur_slice, nslices, touched);

    dx = -ux * extra_slice_size / neslices, dy = -uy * extra_slice_size / neslices;
    time_stamp = linear_interpolate(sx, sy, dx, dy, time_stamp, dur_slice, neslices, touched);
    linear_interpolate(sx, sy, 0, 0, time_stamp, 0, 10, touched);

    msg_touch_up(sx, sy, touched);

    return true;
}

void InputInjector::UgencyClear()
{
    if (stop_possible) StopThread();
    stop_requested = false;
    while (!Empty()) m_msgs.pop();
    SuperToucher::MouseLeftUp();
    //RunInputInjectorMainEssential ess{ m_msgs.get(), m_hwnd};
    m_thread = std::thread(RunInputInjectorMain, this);
}

bool InputInjector::MouseLeftDown(int x, int y)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;
    msg_left_down(x, y);
    return true;
}
bool InputInjector::MouseMove(int x, int y)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;
    msg_left_hover(x, y);
    return true;
}
bool InputInjector::MouseLeftUp(int x, int y)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;
    msg_left_up(x, y);
    return true;
}

UINT64 InputInjector::TouchDown(int x, int y)
{
    LogTraceFunction;
    if (!m_inited) return false;
    if (Exceeded()) return false;
    UINT64 id;
    msg_touch_down(x, y, id);
    return id;
}
bool InputInjector::TouchMove(int x, int y, UINT64 id)
{
    if (!m_inited) return false;
    if (Exceeded()) return false;
    if (!HasTouch(id)) {
        callbackfe("Invalid input id")
            return false;
    }
    msg_touch_update(x, y, id);
    return true;
}
bool InputInjector::TouchUp(int x, int y, UINT64 id)
{
    LogTraceFunction;
    if (!m_inited) return false;
    if (Exceeded()) return false;
    if (!HasTouch(id)) {
        callbackfe("Invalid input id")
        return false;
    }
    msg_touch_up(x, y, id);
    return true;
}

bool InputInjector::Interrupt()
{
    UgencyClear();
    for (auto& i : m_touchids)
    {
        if (!SuperToucher::Up(i.second, 0, 0))
        {
            callbackfe("Error when try to interrupting touch event!!!!");
            return false;
        }
    }
    return true;
}