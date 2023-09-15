#pragma once

void SetCallbackFunction(libWSAController::lib_callback);
extern libWSAController::lib_callback callback;

class LoggerAux
{
public:
    explicit LoggerAux(std::string_view func_name)
        : m_func_name(func_name), m_start_time(std::chrono::steady_clock::now())
    {
        callback(m_func_name, "TRC", "| enter");
    }
    ~LoggerAux()
    {
        const auto duration = std::chrono::steady_clock::now() - m_start_time;
        callback(m_func_name, "TRC", std::format("| leave, {}",
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), "ms"));
    }
    LoggerAux(const LoggerAux&) = default;
    LoggerAux(LoggerAux&&) = default;
    LoggerAux& operator=(const LoggerAux&) = default;
    LoggerAux& operator=(LoggerAux&&) = default;

private:
    std::string m_func_name;
    std::chrono::time_point<std::chrono::steady_clock> m_start_time;
};

#define callbackf(level,msg) callback(__FUNCTION__, level, msg)
#define callbackfv(msg) callbackf("VRB", msg);
#define callbackfi(msg) callbackf("INF", msg);
#define callbackfe(msg) callbackf("ERR", msg);
#define callbackfw(msg) callbackf("WRN", msg);
#define callbackfd(msg) callbackf("DBG", msg);
#define callbackfo(msg) callbackf("?", msg);