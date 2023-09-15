#pragma once

constexpr size_t max_queue_length = 1024;

class InputInjector
{
public:
    ~InputInjector() { Release(); }

	bool Attach(HWND, DWORD processId);
	void Release();

    bool MouseLeftClick(int x, int y);
    bool MouseLeftSwipe(double sx, double sy, double ex, double ey, double dur);
    bool MouseLeftSwipePrecisely(double sx, double sy, double ex, double ey, double dur);

    bool TouchClick(int x, int y);
    bool TouchSwipe(double sx, double sy, double ex, double ey, double dur);
    bool TouchSwipePrecisely(double sx, double sy, double ex, double ey, double dur);

    void UgencyClear();

    bool MouseLeftDown(int x, int y);
    bool MouseMove(int x, int y);
    bool MouseLeftUp(int x, int y);

    UINT64 TouchDown(int x, int y);
    bool TouchMove(int x, int y, UINT64 id);
    bool TouchUp(int x, int y, UINT64 id);

    inline size_t CountTouchedPoints() { return m_touchids.size(); }
    inline bool HasTouch(UINT64 id) { return m_touchids.find(id) != m_touchids.cend(); }
    bool Interrupt();

    void Wait();

private:

	HWND m_hwnd = NULL;
    bool m_inited = false;
    UINT64 stop_possible = false;
    bool stop_requested = false;
    std::thread m_thread;

public:
    struct MsgUnit
    {
        int64_t type;
        WPARAM wParam;
        LPARAM lParam;
    };
    friend void RunInputInjectorMain(InputInjector*);

private:
    std::queue<MsgUnit> m_msgs;
    std::map<UINT64, TouchedPointID> m_touchids;
    int m_caption_height = 45;

    double linear_interpolate(double& sx, double& sy, double dx, double dy,
        double time_stamp, double dur_slice, double n, UINT64 touched = UINT64_MAX);

public:
    inline bool Empty() { return m_msgs.empty(); }
    inline bool Exceeded() { return m_msgs.size() > max_queue_length; }
    inline void StopThread() {
        stop_requested = true;
        if (m_thread.joinable()) m_thread.join();
        stop_possible = false;
    }
};