/*  Gamepad Battery —— Windows 托盘手柄电量
 *
 *  - 通过 Windows 官方 XInput 接口读取 Xbox / XInput 兼容手柄的电量档位
 *  - 托盘图标上直接显示档位数字，颜色随电量变化
 *  - 纯 Win32 + GDI+，不依赖任何第三方库
 *
 *  编译：见仓库根目录 build.bat
 *  许可：MIT
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>

using namespace Gdiplus;

/* ---------- XInput ---------- */
typedef struct { WORD wButtons; BYTE bLT, bRT; SHORT lx, ly, rx, ry; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { BYTE BatteryType; BYTE BatteryLevel; } XINPUT_BATTERY_INFORMATION;
typedef DWORD (WINAPI *PFN_GET_STATE)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *PFN_GET_BATTERY)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION *);

static PFN_GET_STATE   g_get_state   = NULL;
static PFN_GET_BATTERY g_get_battery = NULL;

/* ---------- 全局 ---------- */
static HWND   g_hwnd = NULL;
static NOTIFYICONDATAW g_nid;
static UINT   g_trayMsg = WM_APP + 1;
static UINT   g_taskbarCreated = 0;
static BOOL   g_trayAdded = FALSE;
static HICON  g_iconTray  = NULL;
static wchar_t g_logPath[MAX_PATH];
static int    g_levelNum = -1;
static wchar_t g_level[16] = L"—";
static wchar_t g_sub[96]   = L"未连接";

#define MENU_OPEN 1001
#define MENU_EXIT 1002

static HICON make_icon(int level);

static const Color C_BG_TOP (255, 251, 247, 239);
static const Color C_BG_BOT (255, 239, 230, 214);
static const Color C_CARD   (238, 255, 255, 255);
static const Color C_INK    (255, 34, 30, 24);
static const Color C_MUTED  (255, 138, 128, 117);
static const Color C_ACCENT (255, 140, 28, 19);
static const Color C_TRACK  (255, 232, 224, 210);

/* ---------- 日志（排查用，写在程序同目录） ---------- */
static void logf_(const wchar_t *fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    FILE *f;

    va_start(ap, fmt);
    vswprintf_s(buf, _countof(buf), fmt, ap);
    va_end(ap);

    if (!g_logPath[0]) return;
    f = _wfopen(g_logPath, L"a, ccs=UTF-16LE");
    if (f)
    {
        fwprintf(f, L"%ls\n", buf);
        fclose(f);
    }
}

/* ---------- 绘制工具 ---------- */
static void round_rect(GraphicsPath &p, RectF r, REAL rad)
{
    REAL d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
}

/* ---------- 刷新数据 ---------- */
static void refresh(void)
{
    int idx, found = 0;
    XINPUT_STATE st;
    XINPUT_BATTERY_INFORMATION bat;

    for (idx = 0; idx < 4; idx++)
    {
        ZeroMemory(&st, sizeof(st));
        if (g_get_state((DWORD)idx, &st) != ERROR_SUCCESS) continue;

        found = 1;
        if (g_get_battery((DWORD)idx, 0, &bat) == ERROR_SUCCESS)
        {
            static const wchar_t *lv[] = { L"空", L"低", L"中", L"满" };
            static const wchar_t *tp[] = { L"有线", L"AA 电池", L"充电电池包", L"锂电池" };
            g_levelNum = (bat.BatteryLevel <= 3) ? (int)bat.BatteryLevel : 0;
            wcsncpy_s(g_level, _countof(g_level),
                      (bat.BatteryLevel <= 3) ? lv[bat.BatteryLevel] : L"未知", _TRUNCATE);
            _snwprintf_s(g_sub, _countof(g_sub), _TRUNCATE, L"%ls  ·  槽位 %d",
                         (bat.BatteryType <= 3) ? tp[bat.BatteryType] : L"未知", idx);
        }
        else
        {
            g_levelNum = -1;
            wcscpy_s(g_level, _countof(g_level), L"读不到");
            _snwprintf_s(g_sub, _countof(g_sub), _TRUNCATE, L"手柄未上报电池信息  ·  槽位 %d", idx);
        }
        break;
    }
    if (!found)
    {
        g_levelNum = -1;
        wcscpy_s(g_level, _countof(g_level), L"未连接");
        wcscpy_s(g_sub, _countof(g_sub), L"没有检测到 Xbox 兼容手柄");
    }

    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);

    if (g_trayAdded)
    {
        wchar_t tip[96];
        HICON ni = make_icon(g_levelNum);

        _snwprintf_s(tip, _countof(tip), _TRUNCATE, L"手柄电量：%ls（%d/3）",
                     g_level, (g_levelNum < 0 ? 0 : g_levelNum));
        wcsncpy_s(g_nid.szTip, _countof(g_nid.szTip), tip, _TRUNCATE);

        if (ni)
        {
            HICON old = g_nid.hIcon;
            g_nid.hIcon = ni;
            g_iconTray = ni;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            if (old && old != ni) DestroyIcon(old);
        }
        else
        {
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        }
    }
}

/* ---------- 托盘图标：把电量画进去 ---------- */
static HICON make_icon(int level)
{
    HICON h = NULL;
    {
        Bitmap bmp(32, 32, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.Clear(Color(0, 0, 0, 0));

        Color bg;
        switch (level)
        {
        case 3:  bg = Color(255,  58, 140,  78); break;   /* 满：绿 */
        case 2:  bg = Color(255, 204, 146,  30); break;   /* 中：橙 */
        case 1:  bg = Color(255, 200,  60,  50); break;   /* 低：红 */
        case 0:  bg = Color(255, 150, 142, 132); break;   /* 空：灰 */
        default: bg = Color(255, 118, 112, 104); break;   /* 未连接 */
        }

        GraphicsPath path;
        round_rect(path, RectF(1, 1, 30, 30), 9);
        SolidBrush br(bg);
        g.FillPath(&br, &path);

        {
            wchar_t t[8];
            FontFamily ff(L"微软雅黑");
            Font f(&ff, 20, FontStyleBold, UnitPixel);
            StringFormat sf;
            SolidBrush white(Color(255, 255, 255, 255));

            if (level >= 0) _snwprintf_s(t, _countof(t), _TRUNCATE, L"%d", level);
            else            wcscpy_s(t, _countof(t), L"?");
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(t, -1, &f, RectF(0, 0, 32, 32), &sf, &white);
        }
        bmp.GetHICON(&h);
    }
    return h;
}

/* ---------- 托盘 ---------- */
static void add_tray(void)
{
    BOOL ok;
    DWORD err;

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = g_trayMsg;
    if (g_iconTray) { DestroyIcon(g_iconTray); g_iconTray = NULL; }
    g_iconTray = make_icon(g_levelNum);
    g_nid.hIcon = g_iconTray ? g_iconTray : LoadIconW(NULL, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, _countof(g_nid.szTip), L"手柄电量：读取中…", _TRUNCATE);

    SetLastError(0);
    ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    err = GetLastError();
    g_trayAdded = ok;

    if (ok)
    {
        g_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
        logf_(L"托盘添加成功");
    }
    else
    {
        logf_(L"托盘添加失败 err=%lu", (unsigned long)err);
        SetTimer(g_hwnd, 2, 3000, NULL);
    }
}

static void tray_menu(void)
{
    POINT pt;
    HMENU m = CreatePopupMenu();
    GetCursorPos(&pt);
    AppendMenuW(m, MF_STRING, MENU_OPEN, L"打开面板");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, MENU_EXIT, L"退出");
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

/* ---------- 界面 ---------- */
static void paint(HDC hdc, int w, int h)
{
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    {
        LinearGradientBrush bg(Point(0, 0), Point(0, h), C_BG_TOP, C_BG_BOT);
        g.FillRectangle(&bg, 0, 0, w, h);
    }

    {
        GraphicsPath shadow;
        round_rect(shadow, RectF(22, 52, (REAL)(w - 44), 196), 18);
        SolidBrush sb(Color(26, 0, 0, 0));
        g.FillPath(&sb, &shadow);

        GraphicsPath card;
        round_rect(card, RectF(20, 46, (REAL)(w - 40), 196), 18);
        SolidBrush cb(C_CARD);
        g.FillPath(&cb, &card);
    }

    {
        FontFamily ff(L"微软雅黑");
        SolidBrush ink(C_INK), muted(C_MUTED), accent(C_ACCENT);

        {
            Font fTitle(&ff, 15, FontStyleBold, UnitPixel);
            g.DrawString(L"手柄电量", -1, &fTitle, PointF(24, 16), &ink);

            Font fTag(&ff, 12, FontStyleRegular, UnitPixel);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentFar);
            g.DrawString(L"XInput", -1, &fTag, RectF(0, 17, (REAL)(w - 24), 20), &sf, &muted);
        }

        {
            Font fBig(&ff, 58, FontStyleBold, UnitPixel);
            g.DrawString(g_level, -1, &fBig, PointF(44, 68), &accent);

            if (g_levelNum >= 0)
            {
                wchar_t cnt[16];
                Font fNum(&ff, 30, FontStyleBold, UnitPixel);
                _snwprintf_s(cnt, _countof(cnt), _TRUNCATE, L"%d/3", g_levelNum);
                g.DrawString(cnt, -1, &fNum, PointF(130, 96), &muted);
            }
        }

        {
            Font fSub(&ff, 14, FontStyleRegular, UnitPixel);
            g.DrawString(g_sub, -1, &fSub, PointF(44, 146), &muted);
        }

        {
            REAL x = 44, y = 184, barW = ((REAL)w - 88 - 16) / 3.0f, barH = 12;
            int i;
            for (i = 0; i < 3; i++)
            {
                GraphicsPath seg;
                round_rect(seg, RectF(x + i * (barW + 8), y, barW, barH), 6);
                if (g_levelNum > i)
                {
                    SolidBrush fill(C_ACCENT);
                    g.FillPath(&fill, &seg);
                }
                else
                {
                    SolidBrush track(C_TRACK);
                    g.FillPath(&track, &seg);
                }
            }
        }

        {
            Font fHint(&ff, 11, FontStyleRegular, UnitPixel);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            g.DrawString(L"关闭窗口即缩到托盘  ·  托盘图标右键可退出", -1, &fHint,
                         RectF(0, (REAL)h - 26, (REAL)w, 18), &sf, &muted);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_taskbarCreated && msg == g_taskbarCreated)
    {
        g_trayAdded = FALSE;
        add_tray();
        return 0;
    }

    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 5000, NULL);
        refresh();
        return 0;

    case WM_TIMER:
        if (wp == 2)
        {
            KillTimer(hwnd, 2);
            if (!g_trayAdded) add_tray();
            return 0;
        }
        refresh();
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        paint(mem, rc.right, rc.bottom);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == MENU_OPEN)
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (LOWORD(wp) == MENU_EXIT)
        {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (g_trayAdded) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_iconTray) { DestroyIcon(g_iconTray); g_iconTray = NULL; }
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == g_trayMsg)
        {
            if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
            {
                if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
                else { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            }
            else if (LOWORD(lp) == WM_RBUTTONUP)
            {
                tray_menu();
            }
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    HMODULE mod;
    WNDCLASSW wc;
    MSG msg;
    GdiplusStartupInput gsi;
    ULONG_PTR gpToken = 0;

    /* 日志路径 = 程序所在目录 */
    GetModuleFileNameW(NULL, g_logPath, MAX_PATH);
    {
        wchar_t *slash = wcsrchr(g_logPath, L'\\');
        if (slash) *(slash + 1) = 0;
        wcscat_s(g_logPath, MAX_PATH, L"run.log");
    }
    DeleteFileW(g_logPath);          /* 每次启动重写诊断日志 */
    logf_(L"=== 启动 ===");

    mod = LoadLibraryW(L"xinput1_4.dll");
    if (!mod) mod = LoadLibraryW(L"xinput1_3.dll");
    if (!mod)
    {
        MessageBoxW(NULL, L"找不到 xinput1_4.dll", L"错误", MB_ICONERROR);
        return 1;
    }
    g_get_state   = (PFN_GET_STATE)GetProcAddress(mod, "XInputGetState");
    g_get_battery = (PFN_GET_BATTERY)GetProcAddress(mod, "XInputGetBatteryInformation");
    if (!g_get_state || !g_get_battery)
    {
        MessageBoxW(NULL, L"XInput 函数导出失败", L"错误", MB_ICONERROR);
        return 1;
    }

    GdiplusStartup(&gpToken, &gsi, NULL);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = L"GamepadBatteryWnd";
    if (!RegisterClassW(&wc))
    {
        logf_(L"注册窗口类失败 err=%lu", (unsigned long)GetLastError());
        GdiplusShutdown(gpToken);
        return 1;
    }

    g_hwnd = CreateWindowW(L"GamepadBatteryWnd", L"手柄电量",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, 460, 360,
                           NULL, NULL, hInst, NULL);
    if (!g_hwnd)
    {
        logf_(L"创建窗口失败 err=%lu", (unsigned long)GetLastError());
        GdiplusShutdown(gpToken);
        return 1;
    }

    add_tray();
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);
    logf_(L"进入消息循环");

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gpToken);
    return 0;
}
