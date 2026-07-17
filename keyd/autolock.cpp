#include "keyd/autolock.hpp"

#include <future>
#include <memory>
#include <thread>

#include <windows.h>

#include <wtsapi32.h>

namespace izan::keyd {

namespace {

    constexpr char kWindowClass[] = "IzanKeydWatch";

    LRESULT CALLBACK watch_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        const auto* onEvent
            = reinterpret_cast<std::function<void(const char*)>*>(
                GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (onEvent) {
            if (msg == WM_WTSSESSION_CHANGE && wp == WTS_SESSION_LOCK)
                (*onEvent)("session-lock");
            else if (msg == WM_POWERBROADCAST && wp == PBT_APMSUSPEND)
                (*onEvent)("suspend");
        }
        return DefWindowProcA(hwnd, msg, wp, lp);
    }

    bool register_wts(HWND hwnd)
    {
        // Resolved dynamically to keep the import table lean; wtsapi32
        // is Microsoft-signed and in system32, so both DLL mitigations
        // stay satisfied.
        HMODULE wts = LoadLibraryW(L"wtsapi32.dll");
        if (!wts)
            return false;
        using Fn = BOOL(WINAPI*)(HWND, DWORD);
        auto fn = reinterpret_cast<Fn>(
            GetProcAddress(wts, "WTSRegisterSessionNotification"));
        return fn && fn(hwnd, NOTIFY_FOR_THIS_SESSION);
    }

    void watch_thread(std::string tag,
        std::function<void(const char*)> on_event, std::promise<bool> ready)
    {
        WNDCLASSA wc {};
        wc.lpfnWndProc = watch_proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassA(&wc); // idempotent per process

        // Top-level and hidden — a message-only window would never see
        // the WM_POWERBROADCAST broadcast.
        HWND hwnd = CreateWindowExA(0, kWindowClass, tag.c_str(), WS_OVERLAPPED,
            0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd || !register_wts(hwnd)) {
            ready.set_value(false);
            return;
        }
        auto onEvent = std::make_unique<std::function<void(const char*)>>(
            std::move(on_event));
        SetWindowLongPtrA(
            hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(onEvent.get()));
        ready.set_value(true);

        MSG msg;
        while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

}

bool start_autolock_watch(
    const std::string& tag, std::function<void(const char*)> on_event)
{
    std::promise<bool> ready;
    std::future<bool> up = ready.get_future();
    std::thread(watch_thread, tag, std::move(on_event), std::move(ready))
        .detach();
    return up.get();
}

}
