#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "packets.hpp"
#include <string>
#include <vector>
#include <atomic>

// ── Visual Styles ──────────────────────────────────────────────────────────────
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ── Constants ──────────────────────────────────────────────────────────────────
static constexpr const wchar_t* WND_CLASS = L"ChatClientWnd";
static constexpr const wchar_t* WND_TITLE = L"MemoryPack Chat Client";
static constexpr int CLIENT_W = 740, CLIENT_H = 490;
static constexpr const char* SERVER_IP   = "127.0.0.1";
static constexpr uint16_t    SERVER_PORT = 9001;

enum CtrlId {
    ID_EDIT_USERNAME = 101, ID_BTN_LOGIN,
    ID_EDIT_ROOM,           ID_BTN_JOIN,
    ID_LIST_USERS,          ID_EDIT_CHAT,
    ID_EDIT_MESSAGE,        ID_BTN_SEND, ID_BTN_PM,
};

#define WM_NET_PACKET     (WM_USER + 1)
#define WM_NET_DISCONNECT (WM_USER + 2)

struct NetMessage { PacketId id; std::vector<uint8_t> body; };

// ── Globals ────────────────────────────────────────────────────────────────────
static HWND g_hWnd;
static HWND g_hEditUsername, g_hBtnLogin;
static HWND g_hEditRoom, g_hBtnJoin;
static HWND g_hListUsers, g_hEditChat;
static HWND g_hEditMessage, g_hBtnSend, g_hBtnPM;
static HFONT g_hFont;

static SOCKET g_sock = INVALID_SOCKET;
static HANDLE g_hRecvThread = nullptr;
static std::string g_username;
static std::atomic<bool> g_running{false};

// ── Forward Declarations ───────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI     RecvThread(LPVOID);

// ── UTF-8 <-> Wide ────────────────────────────────────────────────────────────
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}
static std::wstring GetEditText(HWND hEdit) {
    int len = GetWindowTextLengthW(hEdit);
    if (len == 0) return {};
    std::wstring buf(len + 1, 0);
    GetWindowTextW(hEdit, buf.data(), len + 1);
    buf.resize(len);
    return buf;
}

// ── Network Helpers ────────────────────────────────────────────────────────────
static bool RecvExact(SOCKET s, uint8_t* buf, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, (char*)(buf + got), len - got, 0);
        if (n <= 0) return false;
        got += n;
    }
    return true;
}
static bool SendExact(SOCKET s, const uint8_t* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, (const char*)(buf + sent), len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}
static bool SendPacket(PacketId id, const std::vector<uint8_t>& body) {
    if (g_sock == INVALID_SOCKET) return false;
    uint8_t hdr[PACKET_HEADER_SIZE];
    auto pid = static_cast<uint16_t>(id);
    auto blen = static_cast<int32_t>(body.size());
    std::memcpy(hdr, &pid, 2);
    std::memcpy(hdr + 2, &blen, 4);
    if (!SendExact(g_sock, hdr, PACKET_HEADER_SIZE)) return false;
    if (blen > 0) return SendExact(g_sock, body.data(), blen);
    return true;
}
static bool RecvPacket(SOCKET s, PacketId& id, std::vector<uint8_t>& body) {
    uint8_t hdr[PACKET_HEADER_SIZE];
    if (!RecvExact(s, hdr, PACKET_HEADER_SIZE)) return false;
    uint16_t pid; int32_t blen;
    std::memcpy(&pid, hdr, 2);
    std::memcpy(&blen, hdr + 2, 4);
    id = static_cast<PacketId>(pid);
    if (blen > 0) { body.resize(blen); return RecvExact(s, body.data(), blen); }
    body.clear();
    return true;
}

// ── UI Helpers ─────────────────────────────────────────────────────────────────
static void AppendChat(const std::wstring& text) {
    int len = GetWindowTextLengthW(g_hEditChat);
    SendMessageW(g_hEditChat, EM_SETSEL, len, len);
    std::wstring line = (len > 0 ? L"\r\n" : L"") + text;
    SendMessageW(g_hEditChat, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(g_hEditChat, EM_SCROLLCARET, 0, 0);
}

// ── Receive Thread ─────────────────────────────────────────────────────────────
DWORD WINAPI RecvThread(LPVOID) {
    while (g_running.load()) {
        PacketId id; std::vector<uint8_t> body;
        if (!RecvPacket(g_sock, id, body)) {
            if (g_running.load()) PostMessageW(g_hWnd, WM_NET_DISCONNECT, 0, 0);
            break;
        }
        auto* msg = new NetMessage{id, std::move(body)};
        PostMessageW(g_hWnd, WM_NET_PACKET, 0, (LPARAM)msg);
    }
    return 0;
}

// ── Button Handlers ────────────────────────────────────────────────────────────
static void OnLogin() {
    auto wname = GetEditText(g_hEditUsername);
    if (wname.empty()) { MessageBoxW(g_hWnd, L"Enter a username.", L"Error", MB_OK); return; }

    // Connect
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        MessageBoxW(g_hWnd, L"Cannot connect to server.", L"Error", MB_OK);
        closesocket(g_sock); g_sock = INVALID_SOCKET; return;
    }
    g_username = WideToUtf8(wname);
    g_running.store(true);
    g_hRecvThread = CreateThread(nullptr, 0, RecvThread, nullptr, 0, nullptr);

    LoginRequest req{g_username};
    SendPacket(PacketId::LoginRequest, memorypack::Serialize(req));
}

static void OnJoinRoom() {
    auto wroom = GetEditText(g_hEditRoom);
    if (wroom.empty()) return;
    RoomJoinRequest req{WideToUtf8(wroom)};
    SendPacket(PacketId::RoomJoinRequest, memorypack::Serialize(req));
}

static void OnSendChat() {
    auto wmsg = GetEditText(g_hEditMessage);
    if (wmsg.empty()) return;
    RoomChat pkt{"", WideToUtf8(wmsg)};
    SendPacket(PacketId::RoomChat, memorypack::Serialize(pkt));
    SetWindowTextW(g_hEditMessage, L"");
}

static void OnSendPM() {
    int sel = (int)SendMessageW(g_hListUsers, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) { MessageBoxW(g_hWnd, L"Select a user for PM.", L"Info", MB_OK); return; }
    wchar_t target[256]{};
    SendMessageW(g_hListUsers, LB_GETTEXT, sel, (LPARAM)target);
    auto wmsg = GetEditText(g_hEditMessage);
    if (wmsg.empty()) return;
    PrivateChat pkt{"", WideToUtf8(target), WideToUtf8(wmsg)};
    SendPacket(PacketId::PrivateChat, memorypack::Serialize(pkt));
    SetWindowTextW(g_hEditMessage, L"");
}

// ── Packet Handler (UI thread) ─────────────────────────────────────────────────
static void HandleNetPacket(NetMessage* msg) {
    switch (msg->id) {
    case PacketId::LoginResponse: {
        auto r = memorypack::Deserialize<LoginResponse>(msg->body);
        if (r.success) {
            AppendChat(L"[System] Login OK");
            EnableWindow(g_hEditUsername, FALSE);
            EnableWindow(g_hBtnLogin, FALSE);
            EnableWindow(g_hEditRoom, TRUE);
            EnableWindow(g_hBtnJoin, TRUE);
        } else {
            AppendChat(L"[System] Login failed: " + Utf8ToWide(r.message));
            closesocket(g_sock); g_sock = INVALID_SOCKET;
            g_running.store(false);
        }
        break;
    }
    case PacketId::RoomJoinResponse: {
        auto r = memorypack::Deserialize<RoomJoinResponse>(msg->body);
        SendMessageW(g_hListUsers, LB_RESETCONTENT, 0, 0);
        for (auto& u : r.existingUsers)
            SendMessageW(g_hListUsers, LB_ADDSTRING, 0, (LPARAM)Utf8ToWide(u).c_str());
        EnableWindow(g_hEditMessage, TRUE);
        EnableWindow(g_hBtnSend, TRUE);
        EnableWindow(g_hBtnPM, TRUE);
        auto room = GetEditText(g_hEditRoom);
        AppendChat(L"[System] Joined room: " + room);
        break;
    }
    case PacketId::RoomChat: {
        auto r = memorypack::Deserialize<RoomChat>(msg->body);
        AppendChat(Utf8ToWide(r.senderName) + L": " + Utf8ToWide(r.message));
        break;
    }
    case PacketId::PrivateChat: {
        auto r = memorypack::Deserialize<PrivateChat>(msg->body);
        if (r.senderName == g_username)
            AppendChat(L"[PM to " + Utf8ToWide(r.targetName) + L"] " + Utf8ToWide(r.message));
        else
            AppendChat(L"[PM from " + Utf8ToWide(r.senderName) + L"] " + Utf8ToWide(r.message));
        break;
    }
    case PacketId::UserEntered: {
        auto r = memorypack::Deserialize<UserEntered>(msg->body);
        SendMessageW(g_hListUsers, LB_ADDSTRING, 0, (LPARAM)Utf8ToWide(r.username).c_str());
        AppendChat(L"[System] " + Utf8ToWide(r.username) + L" entered the room");
        break;
    }
    case PacketId::UserLeft: {
        auto r = memorypack::Deserialize<UserLeft>(msg->body);
        auto w = Utf8ToWide(r.username);
        int idx = (int)SendMessageW(g_hListUsers, LB_FINDSTRINGEXACT, -1, (LPARAM)w.c_str());
        if (idx != LB_ERR) SendMessageW(g_hListUsers, LB_DELETESTRING, idx, 0);
        AppendChat(L"[System] " + w + L" left the room");
        break;
    }
    default: break;
    }
}

// ── Window Procedure ───────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto S = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                      int x, int y, int w, int h, int id = 0) -> HWND {
            return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                x, y, w, h, hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
        };
        // Top bar
        S(L"STATIC", L"Username:", SS_LEFT, 10, 14, 65, 20);
        g_hEditUsername = S(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 78, 10, 130, 24, ID_EDIT_USERNAME);
        g_hBtnLogin    = S(L"BUTTON", L"Login",  BS_PUSHBUTTON, 216, 10, 60, 26, ID_BTN_LOGIN);
        S(L"STATIC", L"Room:", SS_LEFT, 290, 14, 42, 20);
        g_hEditRoom    = S(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 336, 10, 130, 24, ID_EDIT_ROOM);
        g_hBtnJoin     = S(L"BUTTON", L"Join",   BS_PUSHBUTTON, 474, 10, 60, 26, ID_BTN_JOIN);

        // Middle
        S(L"STATIC", L"Room Members", SS_LEFT, 10, 44, 150, 16);
        g_hListUsers = S(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 10, 62, 150, 368, ID_LIST_USERS);
        g_hEditChat  = S(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                         170, 44, 560, 386, ID_EDIT_CHAT);

        // Bottom bar
        g_hEditMessage = S(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 170, 440, 370, 24, ID_EDIT_MESSAGE);
        g_hBtnSend     = S(L"BUTTON", L"Send",  BS_PUSHBUTTON, 548, 438, 60, 28, ID_BTN_SEND);
        g_hBtnPM       = S(L"BUTTON", L"PM",    BS_PUSHBUTTON, 616, 438, 55, 28, ID_BTN_PM);

        // Font
        EnumChildWindows(hWnd, [](HWND c, LPARAM f) -> BOOL {
            SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return TRUE;
        }, (LPARAM)g_hFont);

        // Initial state: room/chat disabled
        EnableWindow(g_hEditRoom,    FALSE);
        EnableWindow(g_hBtnJoin,     FALSE);
        EnableWindow(g_hEditMessage, FALSE);
        EnableWindow(g_hBtnSend,     FALSE);
        EnableWindow(g_hBtnPM,       FALSE);
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BTN_LOGIN: OnLogin();    break;
        case ID_BTN_JOIN:  OnJoinRoom(); break;
        case ID_BTN_SEND:  OnSendChat(); break;
        case ID_BTN_PM:    OnSendPM();   break;
        }
        break;

    case WM_NET_PACKET: {
        auto* m = reinterpret_cast<NetMessage*>(lParam);
        HandleNetPacket(m);
        delete m;
        break;
    }
    case WM_NET_DISCONNECT:
        AppendChat(L"[System] Disconnected from server");
        break;

    case WM_DESTROY:
        g_running.store(false);
        if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
        if (g_hRecvThread) { WaitForSingleObject(g_hRecvThread, 2000); CloseHandle(g_hRecvThread); }
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ── WinMain ────────────────────────────────────────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName  = WND_CLASS;
    RegisterClassExW(&wc);

    RECT rc{0, 0, CLIENT_W, CLIENT_H};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);
    g_hWnd = CreateWindowExW(0, WND_CLASS, WND_TITLE,
        (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)),
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hWnd, nCmdShow);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    WSACleanup();
    return (int)m.wParam;
}
