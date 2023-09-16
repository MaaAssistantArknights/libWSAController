#include "pch.h"

#include "libWSAController.h"
#include "VirtualTouch.h"
#include "CallbackLogger.h"

#include "Codec.h"


//#define USE_PEN_TOUCH


#if defined(USE_PEN_TOUCH)
#define PT_MYTOUCH PT_PEN
#define POINTER_MYTOUCH_INFO POINTER_PEN_INFO
#define GetPointerXXInfo "GetPointerPenInfo"
#else
#define PT_MYTOUCH PT_TOUCH
#define POINTER_MYTOUCH_INFO POINTER_TOUCH_INFO
#define GetPointerXXInfo "GetPointerTouchInfo"
#endif

namespace SuperToucher
{
    constexpr size_t max_touch_point_count = 512;
    constexpr size_t msg_queue_max_length = 512;

    constexpr size_t jmp_code_size = 32;
    constexpr size_t func_size = 8192;

    typedef BOOL(__stdcall* PFN_WRITEPROCESSMEMORY)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*);
    typedef HANDLE(__stdcall* PFN_GETCURRENTPROCESS)(void);
    typedef SHORT(__stdcall* PFN_GetKeyState)(INT);

    static HANDLE m_target_process = nullptr;
    static HMODULE m_kernel32 = nullptr;
    static HMODULE m_user32 = nullptr;

    static bool m_good = false;
    bool IsReady() { return m_good; }

    struct BeltedQueue
    {
        LPVOID __pNext = nullptr;
        UINT64 isEnd = 0;
    };
    struct MsgQueue : BeltedQueue
    {
        MSG msg = {};
    };
    struct PointsQueue : BeltedQueue
    {
        UINT64 count = 0;
        UINT16 points[max_touch_point_count];
        POINTER_MYTOUCH_INFO info[max_touch_point_count];
    };
    struct TouchedPoint
    {
        HWND dst = NULL;
        UINT16 id = 1;
        DWORD flags = 0xffffffff;
        WPARAM to_wparam()
        {
            return id | (uint64_t(flags) << 16);
        }
    };

    class InjectedCode
    {
    private:
        struct
        {
            LPVOID lpFunction = NULL;
            LPVOID lpGetCurrentProcess = NULL;
            LPVOID lpWriteProcessMemory = NULL;
            LPVOID lpVirtualProtectEx = NULL;
            unsigned char oldcode[jmp_code_size] = {};
            unsigned char newcode[jmp_code_size] = {};
            LPVOID func_addr = NULL;
            UINT64 lParam = 0;
        } m_remote;
        unsigned char m_newcode[jmp_code_size] =
            "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x50\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00\xff\xe0";
        unsigned char m_oldcode[jmp_code_size];

        LPVOID remote_func_addr = nullptr;
        LPVOID remote_para_addr = nullptr;
        LPVOID remote_flag_addr = nullptr;

        size_t more_data_size = 0;
        LPVOID remote_more_data = nullptr;
        LPVOID mapped_more_data = nullptr;

        bool m_need_write_back = false;
        bool m_invasive_injected = false;
        size_t minimal_valid_code_bytes = 1024;
        FARPROC m_target_func = NULL;
    public:
        ~InjectedCode()
        {
            Release();
            MemRelease();
        }

        bool Inject(std::vector<unsigned char> code, FARPROC target, FARPROC helper = nullptr)
        {
            LogTraceFunction;
            if (m_good)
            {
                callbackfi("Ready to hook");

                // 自定义函数地址
                remote_func_addr =
                    VirtualAllocEx(m_target_process, NULL, func_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (remote_func_addr == NULL) {
                    callbackfe("Failed to virtual allock!");
                    Release();
                    return false;
                }

                // 自定义参数地址
                remote_para_addr =
                    VirtualAllocEx(m_target_process, NULL, sizeof(m_remote), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (remote_para_addr == NULL) {
                    callbackfe("Failed to virtual allock!");
                    Release();
                    return false;
                }

                callbackfi(std::format("function addr: {} parameter addr : {}", remote_func_addr, remote_para_addr));

                SIZE_T wrotten_bytes = 0;
                DWORD protection;

                m_remote.lpGetCurrentProcess = (LPVOID)GetProcAddress(m_kernel32, "GetCurrentProcess");
                m_remote.lpWriteProcessMemory = (LPVOID)GetProcAddress(m_kernel32, "WriteProcessMemory");
                m_remote.lpVirtualProtectEx = (LPVOID)GetProcAddress(m_kernel32, "VirtualProtectEx");
                m_remote.lpFunction = (LPVOID)target;
                m_remote.func_addr = (LPVOID)helper;

                *(UINT64*)&(m_newcode[2]) = ((UINT64)remote_para_addr);
                *(UINT64*)&(m_newcode[13]) = ((UINT64)remote_func_addr);

                memcpy(m_oldcode, m_remote.lpFunction, jmp_code_size);
                {
                    std::ostringstream str_codes;
                    str_codes << std::hex;
                    for (auto i : m_oldcode) {
                        str_codes << "\\x" << (int)i;
                    }
                    callbackfi(std::format("Old codes: {}", str_codes.str()));
                }
                memcpy(m_remote.oldcode, m_oldcode, jmp_code_size);
                memcpy(m_remote.newcode, m_newcode, jmp_code_size);

                // 写自定义函数
                VirtualProtectEx(m_target_process, (LPVOID)remote_func_addr, code.size(), PAGE_READWRITE, &protection);
                if (!WriteProcessMemory(m_target_process, (LPVOID)remote_func_addr, (LPCVOID)code.data(), code.size(),
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 1! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                VirtualProtectEx(m_target_process, (LPVOID)remote_func_addr, code.size(), protection, &protection);

                if (more_data_size)
                    m_remote.lParam = (UINT64)remote_more_data;
                // 写自定义参数
                VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), PAGE_READWRITE, &protection);
                if (!WriteProcessMemory(m_target_process, (LPVOID)remote_para_addr, (LPVOID)&m_remote, sizeof(m_remote),
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 2! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), protection, &protection);

                // 覆写系统跳转
                VirtualProtectEx(m_target_process, (LPVOID)m_remote.lpFunction, jmp_code_size, PAGE_EXECUTE_READWRITE, &protection);
                if (!WriteProcessMemory(m_target_process, (LPVOID)m_remote.lpFunction, (LPVOID)m_newcode, jmp_code_size,
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 3! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                if (wrotten_bytes != jmp_code_size) {
                    callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
                    m_need_write_back = true;
                    Release();
                    return false;
                }

                remote_flag_addr = (LPVOID)((UINT64)remote_para_addr + (UINT64)&m_remote.lParam - (UINT64)&m_remote);
                m_need_write_back = true;

                callbackfi("Hook succeeded.");
                return true;
            }
            return false;
        }
        bool InvasiveInject(std::vector<unsigned char> code, FARPROC target, FARPROC helper = nullptr)
        {
            LogTraceFunction;
            if (m_good)
            {
                m_invasive_injected = true;
                m_target_func = target;

                callbackfi("Ready to hook");

                // 自定义函数地址
                remote_func_addr =
                    VirtualAllocEx(m_target_process, NULL, func_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (remote_func_addr == NULL) {
                    callbackfe("Failed to virtual allock!");
                    Release();
                    return false;
                }

                // 自定义参数地址
                remote_para_addr =
                    VirtualAllocEx(m_target_process, NULL, sizeof(m_remote), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (remote_para_addr == NULL) {
                    callbackfe("Failed to virtual allock!");
                    Release();
                    return false;
                }

                callbackfi(std::format("function addr: {} parameter addr : {}, spring addr: {}",
                    remote_func_addr, remote_para_addr, m_remote.lpFunction));

                SIZE_T wrotten_bytes = 0;
                DWORD protection;

                m_remote.func_addr = (LPVOID)helper;

                if (more_data_size)
                    m_remote.lParam = (UINT64)remote_more_data;
                // 写自定义参数
                VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), PAGE_READWRITE, &protection);
                if (!WriteProcessMemory(m_target_process, (LPVOID)remote_para_addr, (LPVOID)&m_remote, sizeof(m_remote),
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 2! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), protection, &protection);

                // 转移源指令
                minimal_valid_code_bytes = 0x10;
                VirtualProtectEx(m_target_process, (LPVOID)target, minimal_valid_code_bytes, PAGE_EXECUTE_READWRITE, &protection);
                VirtualProtectEx(m_target_process, (LPVOID)remote_func_addr, func_size, PAGE_READWRITE, &protection);
                std::unique_ptr<unsigned char>jmp_opcode_buffer(new unsigned char[minimal_valid_code_bytes]);
                if (!ReadProcessMemory(m_target_process, (LPVOID)target, (LPVOID)jmp_opcode_buffer.get(), minimal_valid_code_bytes,
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 2.3! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                memcpy(m_oldcode, jmp_opcode_buffer.get(), minimal_valid_code_bytes);
                // 写自定义函数
                unsigned char lead_in_param_opcode[] = {
                    0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56,
                    0x34, 0x12, 0x50,
                };
                *((UINT64*)(lead_in_param_opcode + 2)) = ((UINT64)remote_para_addr);
                code.insert(code.cbegin(), lead_in_param_opcode, lead_in_param_opcode + sizeof(lead_in_param_opcode));
                if (!WriteProcessMemory(m_target_process, (LPVOID)remote_func_addr, (LPCVOID)code.data(), code.size(),
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 1! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                if (!WriteProcessMemory(m_target_process, (LPVOID)((UINT64)remote_func_addr + code.size()),
                    (LPVOID)jmp_opcode_buffer.get(), minimal_valid_code_bytes, &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 2.6! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                unsigned char jmp_code_back[] = {
                    0x48, 0x89, 0x44, 0x24, 0xe8, 0x48, 0xb8, 0x34,
                    0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x48,
                    0x89, 0x44, 0x24, 0xf0, 0x48, 0x8b, 0x44, 0x24,
                    0xe8, 0xff, 0x64, 0x24, 0xf0,
                };
                *((UINT64*)(jmp_code_back + 7)) = (UINT64)target + minimal_valid_code_bytes;
                if (!WriteProcessMemory(m_target_process, (LPVOID)((UINT64)remote_func_addr + code.size() + minimal_valid_code_bytes),
                    (LPVOID)jmp_code_back, sizeof(jmp_code_back), &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 2.6! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                VirtualProtectEx(m_target_process, (LPVOID)remote_func_addr, func_size, PAGE_EXECUTE, &protection);

                // 覆写系统跳转
                unsigned char jmp_code_out[] = {
                    0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x10, 0xff, 0xe0
                };
                *((UINT64*)(jmp_code_out + 2)) = (UINT64)remote_func_addr;
                if (!WriteProcessMemory(m_target_process, (LPVOID)target, (LPVOID)jmp_code_out, sizeof(jmp_code_out),
                    &wrotten_bytes)) {
                    callbackfe(std::format("Failed to write memory, category: 3! GetLastError() = {}", GetLastError()));
                    Release();
                    return false;
                }
                if (wrotten_bytes != sizeof(jmp_code_out)) {
                    callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
                    m_need_write_back = true;
                    Release();
                    return false;
                }
                VirtualProtectEx(m_target_process, (LPVOID)target, minimal_valid_code_bytes, PAGE_EXECUTE_READ, &protection);

                remote_flag_addr = (LPVOID)((UINT64)remote_para_addr + (UINT64)&m_remote.lParam - (UINT64)&m_remote);
                m_need_write_back = true;

                callbackfi("Hook succeeded.");
                return true;
            }
            return false;
        }
        void Release()
        {
            LogTraceFunction;
            if (!m_good && m_need_write_back)
            {
                std::ostringstream str_codes;
                str_codes << std::hex;
                for (auto i : m_oldcode) {
                    str_codes << "\\x" << (int)i;
                }
                callbackfe(std::format("[[FATAL]] m_target_process=nullptr before write back old codes, "
                    "write it back manually if needed! old_codes = {}", str_codes.str()));
                return;
            }
            if (m_invasive_injected)
            {
                SIZE_T wrotten_bytes;
                DWORD protection;
                VirtualProtectEx(m_target_process, (LPVOID)m_target_func, minimal_valid_code_bytes, PAGE_EXECUTE_READWRITE, &protection);
                if (!WriteProcessMemory(m_target_process, (LPVOID)m_target_func, (LPCVOID)m_oldcode,
                    minimal_valid_code_bytes, &wrotten_bytes)) {
                    callbackfe(std::format("Cannot write the old codes(Invasive). GetLastError() = {}", GetLastError()));
                }
                if (wrotten_bytes != minimal_valid_code_bytes) {
                    callbackfe(std::format("Wrong code size(Invasive)! GetLastError() = {}", GetLastError()));
                }
                VirtualProtectEx(m_target_process, (LPVOID)m_target_func, jmp_code_size, PAGE_EXECUTE, &protection);
            }
            else if (m_need_write_back)
            {
                SIZE_T wrotten_bytes;
                DWORD protection;
                if (!WriteProcessMemory(m_target_process, (LPVOID)m_remote.lpFunction, (LPCVOID)m_remote.oldcode, jmp_code_size,
                    &wrotten_bytes)) {
                    callbackfe(std::format("Cannot write the old codes. GetLastError() = {}", GetLastError()));
                }
                if (wrotten_bytes != jmp_code_size) {
                    callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
                }
                VirtualProtectEx(m_target_process, (LPVOID)m_remote.lpFunction, jmp_code_size, PAGE_EXECUTE, &protection);
                m_need_write_back = false;
            }
            if (remote_func_addr) {
                VirtualFree(remote_func_addr, 0, MEM_RELEASE);
                remote_func_addr = nullptr;
            }
            if (remote_para_addr) {
                VirtualFree(remote_para_addr, 0, MEM_RELEASE);
                remote_para_addr = nullptr;
            }
            MemRelease();
        }

        template <typename T = UINT64>
        T GetLParam() const { return (T)m_remote.lParam; }
        // 失败返回UINT64_MAX
        template <typename T = UINT64>
        T QuerryLParam()
        {
            // 读自定义参数
            SIZE_T read_bytes;
            DWORD protection;
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), PAGE_READWRITE, &protection);
            if (!ReadProcessMemory(m_target_process, (LPVOID)remote_flag_addr, (LPVOID)&m_remote.lParam, sizeof(UINT64),
                &read_bytes)) {
                callbackfe(std::format("Failed to read memory, category: 7! GetLastError() = {}", GetLastError()));
                Release();
                return (T)ULLONG_MAX;
            }
            if (read_bytes != sizeof(UINT64)) {
                callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
            }
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), protection, &protection);
            return GetLParam<T>();
        }
        // 返回值：是否进行远程写入
        bool SetLParam(auto lp, size_t offset = 0)
        {
            m_remote.lParam = (UINT64)lp + offset;
            if (!m_good || !remote_para_addr) return false;
            // 重写自定义参数
            SIZE_T wrotten_bytes;
            DWORD protection;
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), PAGE_READWRITE, &protection);
            if (!WriteProcessMemory(m_target_process, (LPVOID)remote_flag_addr, (LPVOID)&m_remote.lParam, sizeof(UINT64),
                &wrotten_bytes)) {
                callbackfe(std::format("Failed to write memory, category: 4! GetLastError() = {}", GetLastError()));
                Release();
                m_good = false;
                return false;
            }
            if (wrotten_bytes != sizeof(UINT64)) {
                callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
            }
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), protection, &protection);
            return true;
        }
        bool SetLParamToData(size_t offset = 0)
        {
            m_remote.lParam = (UINT64)remote_more_data + offset;
            // 重写自定义参数
            SIZE_T wrotten_bytes;
            DWORD protection;
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), PAGE_READWRITE, &protection);
            if (!WriteProcessMemory(m_target_process, (LPVOID)remote_flag_addr, (LPVOID)&m_remote.lParam, sizeof(UINT64),
                &wrotten_bytes)) {
                callbackfe(std::format("Failed to write memory, category: 4! GetLastError() = {}", GetLastError()));
                Release();
                m_good = false;
                return false;
            }
            if (wrotten_bytes != sizeof(UINT64)) {
                callbackfe(std::format("Wrong code size! GetLastError() = {}", GetLastError()));
            }
            VirtualProtectEx(m_target_process, (LPVOID)remote_para_addr, sizeof(m_remote), protection, &protection);
            return true;
        }

        bool MemMoreAlloc(size_t sz)
        {
            if (remote_more_data) return false;
            remote_more_data =
                VirtualAllocEx(m_target_process, NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (remote_more_data == NULL) {
                callbackfe(std::format("Failed to virtual allock more space! GetLastError() = ",
                    GetLastError()));
                return false;
            }
            more_data_size = sz;
            return true;
        }
        bool MemRelease()
        {
            LogTraceFunction;
            if (mapped_more_data)
            {
                delete[] mapped_more_data;
                mapped_more_data = nullptr;
            }
            if (remote_more_data) {
                VirtualFree(remote_more_data, 0, MEM_RELEASE);
                remote_more_data = nullptr;
            }
            return true;
        }
        bool MemMap(LPVOID* field)
        {
            if (!field) return false;
            if (mapped_more_data == nullptr)
                mapped_more_data = new unsigned char[more_data_size];
            if (!Read(mapped_more_data, more_data_size, 0)) return false;
            *field = mapped_more_data;
            return (*field != nullptr);
        }
        bool MemUnmap()
        {
            bool result = Write(mapped_more_data, more_data_size, 0);
            delete[] mapped_more_data;
            mapped_more_data = nullptr;
            return result;
        }
        bool Write(LPVOID src, size_t sz = 0, size_t offset = 0)
        {
            // LogTraceFunction;
            if (!remote_more_data)
            {
                callbackfe("No space allocated before!");
                return false;
            }

            SIZE_T wrotten_bytes = 0;
            LPVOID dst = (LPVOID)((UINT64)remote_more_data + offset);
            sz = ((sz == 0) ? more_data_size : sz);
            if (!WriteProcessMemory(m_target_process, dst, src, sz, &wrotten_bytes))
            {
                callbackfe(std::format("Error occurred when writing more data, GetLastError() = {}", GetLastError()));
                return false;
            }
            if (wrotten_bytes != sz)
            {
                callbackfe(std::format("Wrote wrong bytes! GetLastError() = {}", GetLastError()));
                return false;
            }

            return true;
        }
        bool Read(LPVOID src, size_t sz = 0, size_t offset = 0)
        {
            // LogTraceFunction;
            if (!remote_more_data)
            {
                callbackfe("No space allocated before!");
                return false;
            }

            SIZE_T read_bytes = 0;
            LPVOID dst = (LPVOID)((UINT64)remote_more_data + offset);
            sz = ((sz == 0) ? more_data_size : sz);
            if (!ReadProcessMemory(m_target_process, dst, src, sz, &read_bytes))
            {
                callbackfe(std::format("Error occurred when reading more data, GetLastError() = {}", GetLastError()));
                return false;
            }
            if (read_bytes != sz)
            {
                callbackfe(std::format("Wrote wrong bytes! GetLastError() = {}", GetLastError()));
                return false;
            }

            return true;
        }
        template <typename T = size_t>
        T GetRemoteMemAddr() const { return (T)remote_more_data; }
    };

    template <typename T = BeltedQueue>
    requires std::derived_from<T, BeltedQueue>
    class ListQueue
    {
    private:
        struct BindedAddress {
            LPVOID base;
            BindedAddress(LPVOID _ptr = nullptr) : base(_ptr) {}
            operator bool () { return base != nullptr; }
            operator T* () { return (T*)(base); }
            T& operator[](size_t idx) { return ((T*)base)[idx]; }
            BindedAddress& operator= (LPVOID _ptr) { base = _ptr; return *this; };
        } m_mapped = nullptr;
        size_t m_count = 0;
        size_t m_last = 0, m_current = 0, m_buffer_size = 0;
        InjectedCode* m_binded = nullptr;

        bool m_inited = false;

        inline size_t GetNext() { 
            auto _next = m_current + m_buffer_size;
            if (_next > m_count)
                callbackfd("Event circle round complete.");
            return _next % m_count;
        }
        inline bool WillExceed() { return GetNext() + 1 == m_last; }

        void SyncQueueCompletionState()
        {
            m_last = std::distance(m_binded->GetRemoteMemAddr<T*>(),
                m_binded->QuerryLParam<T*>());
        }

    public:
        ListQueue(InjectedCode* bindCode, size_t count)
            : m_binded(bindCode)
        {
            if (!m_binded->MemMap(&m_mapped.base)) return;
            m_count = count;
            m_inited = true;
            for (size_t i = 0; i < m_count - 1; i++)
                m_mapped[i].__pNext = m_binded->GetRemoteMemAddr<T*>() + i + 1;
            m_mapped[m_count - 1].__pNext = m_binded->GetRemoteMemAddr<T*>();
            m_mapped[0].isEnd = 1;
            m_inited &= m_binded->Write(m_mapped.base);
        }

        inline bool IsReady() { return m_inited; }

        bool PushToBuffer(T& item)
        {
            if (!IsReady()) { callbackfe("Failed to add event, due to NOT INITED!"); return false; }
            if (WillExceed()) SyncQueueCompletionState();
            if (WillExceed()) { callbackfw("Event NOT added, due to EXCEEDED!"); return false; }
            // 不要复制pNext
            auto bakup_pNext = m_mapped[GetNext()].__pNext;
            m_mapped[GetNext()] = item;
            m_mapped[GetNext()].__pNext = bakup_pNext;
            m_buffer_size++;
            return true;
        }

        bool CommitBuffer()
        {
            if (m_buffer_size == 0) { callbackfw("NO buffer to be committed."); return false; }
            if (!IsReady()) { callbackfe("Failed to commit event buffer, due to NOT INITED!"); return false; }
            m_mapped[m_current].isEnd = 1;
            m_mapped[GetNext()].isEnd = 1;
            if (!m_binded->Write(m_mapped.base))
            {
                callbackfe(
                    std::format("Failed to write buffer, due to function `Write` failed, GetLastError() = {}", GetLastError())
                );
                return false;
            }
            // 开关
            m_mapped[m_current].isEnd = 0;
            m_binded->Write(&m_mapped[m_current].isEnd, 8, m_current * sizeof(T) + 8);
            m_current = GetNext();
            m_buffer_size = 0;
            return true;
        }

        void Wait()
        {
            while (m_last != m_current)
            {
                SyncQueueCompletionState();
                std::this_thread::yield();
            }
        }

    };

    constexpr DWORD touch_model_down = POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT |
        POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_DOWN;
    constexpr DWORD touch_model_update = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT |
        POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_PRIMARY | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_DOWN | POINTER_FLAG_UPDATE;
    constexpr DWORD touch_model_up = POINTER_FLAG_INRANGE | POINTER_FLAG_PRIMARY |
        POINTER_FLAG_CONFIDENCE | POINTER_FLAG_UP;
    class AllPointsController
    {
    private:
        std::vector<std::unique_ptr<ListQueue<PointsQueue>>> m_mem_resolvers;
        std::map<UINT16, POINTER_MYTOUCH_INFO> m_infos;

        bool SaveBuf()
        {
            PointsQueue to_push;
            to_push.count = m_infos.size();
            UINT64 idx = 0;
            for (auto& i : m_infos)
            {
                to_push.points[idx] = i.first;
                to_push.info[idx] = i.second;
                idx++;
            }
            for (auto& i : m_mem_resolvers)
                if (!i->PushToBuffer(to_push))
                    return false;
            return true;
        }

    public:
        void Wait()
        {
            for (auto& i : m_mem_resolvers)
                i->Wait();
        }

        bool BindOne(InjectedCode* pIC)
        {
            if (pIC == nullptr) return false;
            if (pIC->GetRemoteMemAddr<LPVOID>() != nullptr)
            {
                callbackfe("Failed to bind a InjectedCode due to pre allocated memory!");
                return false;
            }
            pIC->MemMoreAlloc(sizeof(PointsQueue) * msg_queue_max_length);
            std::unique_ptr<ListQueue<PointsQueue>> mem_resolver =
                std::make_unique<ListQueue<PointsQueue>>(pIC, msg_queue_max_length);
            m_mem_resolvers.emplace_back(std::move(mem_resolver));
            return true;
        }
        bool ActionAddPoint(TouchedPoint pt, int x, int y)
        {
            if (m_infos.find(pt.id) != m_infos.cend()) return false;
            m_infos[pt.id] = POINTER_MYTOUCH_INFO{
                .pointerInfo = {
                    .pointerType = PT_MYTOUCH,
                    .pointerId = pt.id,
                    .frameId = (UINT32)rand(),
                    .pointerFlags = pt.flags,
                    .sourceDevice = (HANDLE)374333428079975,
                    .hwndTarget = pt.dst,
                    .ptPixelLocation = { x, y },
                    .ptHimetricLocation = { long(x * 2.03), long(y * 3.4) },
                    .ptPixelLocationRaw = { x, y },
                    .ptHimetricLocationRaw = { long(x * 2.03), long(y * 3.4) },
                    .dwTime = GetTickCount(),
                    .historyCount = 1,
                    .InputData = 0,
                    .dwKeyStates = 0,
                    .PerformanceCount = GetTickCount64(),
                    .ButtonChangeType = POINTER_CHANGE_FIRSTBUTTON_DOWN
                }
            };
            return SaveBuf();
        }
        bool ActionUpdate(UINT16 id, int x, int y)
        {
            if (m_infos.find(id) == m_infos.cend()) return false;
            m_infos[id].pointerInfo.pointerFlags = touch_model_update;
            m_infos[id].pointerInfo.dwTime = GetTickCount();
            m_infos[id].pointerInfo.PerformanceCount = GetTickCount64();
            m_infos[id].pointerInfo.ButtonChangeType = POINTER_CHANGE_NONE;
            m_infos[id].pointerInfo.frameId++;
            return SaveBuf();
        }
        bool ActionDeletePoint(UINT16 id, int x, int y)
        {
            if (m_infos.find(id) == m_infos.cend()) return false;
            m_infos[id].pointerInfo.pointerFlags = touch_model_up;
            m_infos[id].pointerInfo.dwTime = GetTickCount();
            m_infos[id].pointerInfo.PerformanceCount = GetTickCount64();
            m_infos[id].pointerInfo.ButtonChangeType = POINTER_CHANGE_FIRSTBUTTON_UP;
            m_infos[id].pointerInfo.frameId++;
            bool result = SaveBuf();
            m_infos.erase(id);
            return result;
        }
        bool CommitActions()
        {
            for (auto& i : m_mem_resolvers)
                if (!i->CommitBuffer())
                    return false;
            return true;
        }
    };

    std::map<TouchedPointID, TouchedPoint> allPoints;
    static size_t touchIDPool = 0;
    static WORD pointerIDPool = 0xff;
    inline TouchedPointID MakeTouchedPointerID() { return touchIDPool++; }
    inline WORD MakeWinPointerID() { return pointerIDPool++; }

    std::unique_ptr<InjectedCode> mouseInject;
    std::unique_ptr<InjectedCode> touchInject;
    std::unique_ptr<ListQueue<MsgQueue>> touchMsgs;
    std::unique_ptr<InjectedCode> fn_GetPointerType;
    std::unique_ptr<InjectedCode> fn_GetPointerTouchInfo;
    //std::unique_ptr<InjectedCode> fn_;
    std::unique_ptr<AllPointsController> pointsPool;

    bool Attach(DWORD pid)
    {
        LogTraceFunction;

        if (m_good) return true;

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
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;                     // 特权启用
            if (!AdjustTokenPrivileges(token, FALSE, &tkp, sizeof(tkp), NULL, NULL)) // 启用指定访问令牌的特权
            {
                CloseHandle(token);
                return false;
            }
        }

        m_target_process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        if (m_target_process == NULL) {
            callbackfe(std::format("OpenProcess Error! GetLastError() = {}", GetLastError()));
            return false;
        }

        m_kernel32 = LoadLibrary(TEXT("kernel32.dll"));
        if (m_kernel32 == 0) {
            callbackfe("Failed to load kernel32.dll");
            Release();
            return false;
        }
        m_user32 = LoadLibrary(TEXT("user32.dll"));
        if (m_user32 == 0) {
            callbackfe("Failed to load user32.dll");
            Release();
            return false;
        }
        m_good = true;

        mouseInject = std::make_unique<InjectedCode>();
        if (!mouseInject->Inject(GetCode_GetKeyState_x64(), GetProcAddress(m_user32, "GetKeyState")))
        {
            callbackfe("Failed to inject code of `GetKeyState()`!");
            Release();
            return false;
        }
        touchInject = std::make_unique<InjectedCode>();
        if (!touchInject->MemMoreAlloc(msg_queue_max_length * sizeof(MsgQueue)))
        {
            callbackfe("Failed to alloc more memory for WinMsgQueue");
        }
        touchMsgs = std::make_unique<ListQueue<MsgQueue>>(touchInject.get(), msg_queue_max_length);
        if (!touchMsgs->IsReady())
        {
            callbackfe("touchMsgs initialization failed!!!");
            Release();
            return false;
        }
        if (!touchInject->InvasiveInject(GetCode_GetMessageW_Invasive_x64(),
            GetProcAddress(m_user32, "GetMessageW"),
            GetProcAddress(m_user32, "SendMessageW")))
        {
            callbackfe("Failed to inject code of `GetMessageW()`!");
            Release();
            return false;
        }

        pointsPool = std::make_unique<AllPointsController>();
        fn_GetPointerType = std::make_unique<InjectedCode>();
        fn_GetPointerTouchInfo = std::make_unique<InjectedCode>();
        if (!pointsPool->BindOne(fn_GetPointerType.get()))
        {
            callbackfe("Failed to bind a injected code with a message pool!");
            Release();
            return false;
        }
        //if (!pointsPool->BindOne(fn_GetPointerTouchInfo.get()))
        //{
        //    callbackfe("Failed to bind a injected code with a message pool!");
        //    Release();
        //    return false;
        //}
        if (!fn_GetPointerType->Inject(GetCode_GetPointerType_1_x64(PT_MYTOUCH), GetProcAddress(m_user32, "GetPointerType")))
        {
            callbackfe("Failed to inject code of `GetPointerType()`!");
            Release();
            return false;
        }
        //if (!fn_GetPointerTouchInfo->Inject(GetCode_GetPointerTouchInfo_x64(sizeof(POINTER_MYTOUCH_INFO) / sizeof(UINT64)),
        //    GetProcAddress(m_user32, GetPointerXXInfo)))
        if (!fn_GetPointerTouchInfo->Inject(GetCode_GetPointerTouchInfo_aaa_x64(),
            GetProcAddress(m_user32, GetPointerXXInfo)))
        {
            callbackfe("Failed to inject code of `GetPointerXXXInfo()`!");
            Release();
            return false;
        }

        return true;
    }

    void Release()
    {
        LogTraceFunction;

        pointsPool.reset();
        touchMsgs.reset();
        touchInject.reset();
        fn_GetPointerType.reset();
        fn_GetPointerTouchInfo.reset();

        mouseInject.reset();

        if (m_target_process) {
            CloseHandle(m_target_process);
            m_target_process = NULL;
        }
        if (m_kernel32) {
            FreeLibrary(m_kernel32);
            m_kernel32 = NULL;
        }
        if (m_user32) {
            FreeLibrary(m_user32);
            m_user32 = NULL;
        }
        m_good = false;
    }

    std::optional<TouchedPointID> Down(HWND dst, int x, int y, bool closed)
    {
        LogTraceFunction;

        if (allPoints.size() == max_touch_point_count)
        {
            callbackfe("Cannot append touch point. MAX_COUNT!");
            return std::nullopt;
        }

        TouchedPoint pt{
            .dst = dst,
            .id = MakeWinPointerID(),
            .flags = touch_model_down
        };
        if (!pointsPool->ActionAddPoint(pt, x, y))
        {
            callbackfe("Cannot add a point to points pool!");
            return std::nullopt;
        }

        MsgQueue ptmsg;
        ptmsg.msg = MSG{
            .hwnd = pt.dst,
            .message = WM_POINTERDOWN,
            .wParam = pt.to_wparam(),
            .lParam = MAKELPARAM(x, y),
            .time = GetTickCount(),
            .pt = { x, y }
        };
        if (!touchMsgs->PushToBuffer(ptmsg))
        {
            callbackfe("Cannot push message queue buffer.");
            return std::nullopt;
        }
        if (closed && (!pointsPool->CommitActions() || !touchMsgs->CommitBuffer()))
        {
            callbackfe("Failed to commit buffer.");
            return std::nullopt;
        }

        TouchedPointID result = MakeTouchedPointerID();
        allPoints[result] = pt;
        return result;
    }

    bool Update(TouchedPointID p, int x, int y, bool closed)
    {
        LogTraceFunction;

        if (allPoints.find(p) == allPoints.cend()) {
            callbackfe("No such point !!!!");
            return false;
        }
        TouchedPoint& pt = allPoints[p];
        pt.flags = touch_model_update;
        if (!pointsPool->ActionUpdate(pt.id, x, y))
        {
            callbackfe("Cannot add an `update` message to points pool!");
            return false;
        }

        MsgQueue ptmsg;
        ptmsg.msg = MSG{
            .hwnd = pt.dst,
            .message = WM_POINTERUPDATE,
            .wParam = pt.to_wparam(),
            .lParam = MAKELPARAM(x, y),
            .time = GetTickCount(),
            .pt = { x, y }
        };
        if (!touchMsgs->PushToBuffer(ptmsg))
        {
            callbackfe("Cannot push message queue buffer.");
            return false;
        }
        if (closed && (!pointsPool->CommitActions() || !touchMsgs->CommitBuffer()))
        {
            callbackfe("Failed to commit buffer.");
            return false;
        }

        return true;
    }

    bool Up(TouchedPointID p, int x, int y, bool closed)
    {
        LogTraceFunction;

        if (allPoints.find(p) == allPoints.cend()) {
            callbackfe("No such point !!!!");
            return false;
        }
        TouchedPoint pt = allPoints[p];
        pt.flags = touch_model_up;
        if (!pointsPool->ActionDeletePoint(pt.id, x, y))
        {
            callbackfe("Cannot add a `deleted` message to points pool");
            return false;
        }
        allPoints.erase(p);

        MsgQueue ptmsg;
        ptmsg.msg = MSG{
            .hwnd = pt.dst,
            .message = WM_POINTERUP,
            .wParam = pt.to_wparam(),
            .lParam = MAKELPARAM(x, y),
            .time = GetTickCount(),
            .pt = { x, y }
        };
        if (!touchMsgs->PushToBuffer(ptmsg))
        {
            callbackfe("Cannot push message queue buffer.");
            return false;
        }
        if (closed && (!pointsPool->CommitActions() || !touchMsgs->CommitBuffer()))
        {
            callbackfe("Failed to commit buffer.");
            return false;
        }

        return true;
    }
    
    bool UpAll()
    {
        LogTraceFunction;

        POINT ptc;
        GetCursorPos(&ptc);
        if (allPoints.size() > 0)
        {
            bool result = true;
            for (auto& i : allPoints)
            {
                TouchedPoint& pt = i.second;
                pt.flags = touch_model_up;
                if (!pointsPool->ActionDeletePoint(pt.id, ptc.x, ptc.y))
                {
                    callbackfe("Cannot add a `deleted` message to points pool");
                    return false;
                }

                MsgQueue ptmsg;
                ptmsg.msg = MSG{
                    .hwnd = pt.dst,
                    .message = WM_POINTERUP,
                    .wParam = pt.to_wparam(),
                    .lParam = MAKELPARAM(ptc.x, ptc.y),
                    .time = GetTickCount(),
                    .pt = { ptc.x, ptc.y }
                };
                if (!touchMsgs->PushToBuffer(ptmsg))
                {
                    callbackfe("Cannot push message queue buffer.");
                    return false;
                }
            }
            if (!pointsPool->CommitActions() || !touchMsgs->CommitBuffer())
            {
                callbackfe("Failed to commit buffer.");
                return false;
            }
            allPoints.clear();
        }
        return true;
    }

    void WaitTouch()
    {
        LogTraceFunction;
        pointsPool->Wait();
        touchMsgs->Wait();
    }

    bool MouseLeftDown()
    {
        if (!m_good) return false;
        return  mouseInject->SetLParam(1);
    }

    bool MouseLeftUp()
    {
        if (!m_good) return false;
        return mouseInject->SetLParam(0);
    }
}