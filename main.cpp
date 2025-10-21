// Դ�뱣��Ϊ CP936��ANSI �������ģ���
// ���룺
// windres resource.rc -O coff --codepage=936 -o resource.res
// g++ main.cpp resource.res -municode -mwindows -o "�����ѻ�������.exe" -luser32 -lgdi32 -lcomctl32 -lwinmm -lshell32 -ladvapi32 -finput-charset=CP936 -fexec-charset=CP936

#include <windows.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <wchar.h>
#include <shlobj.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

// ȫ��
HINSTANCE g_hInst;
HWND g_hWnd;
volatile BOOL running = TRUE;

// �ȼ�Ĭ��
volatile UINT vkBhop   = VK_F6; // ����
volatile UINT vkCrouch = VK_F7; // ˫��
volatile UINT vkHasten = VK_F8; // ���ٽű�
volatile UINT vkAutoAS = VK_F9; // �Զ�����

// ����ģʽ״̬���ȴ��û����룩
volatile BOOL capBhop   = FALSE;
volatile BOOL capCrouch = FALSE;
volatile BOOL capHasten = FALSE;
volatile BOOL capAutoAS = FALSE;

// ״̬
volatile BOOL bhopActive = FALSE;
volatile BOOL crouchHeld = FALSE;
volatile BOOL hastenHeld = FALSE;
volatile BOOL autoASHeld = FALSE;

// �¼��빳��
HANDLE evtBhopStart, evtCrouchStart, evtHastenStart, evtAutoASStart;
HHOOK hKbdHook, hMouseHook;

// ����
int   bhopDelay       = 3;
int   crouchDown      = 29;
int   crouchUp        = 4;
int   hastenAmplitude = 150;
int   hastenDelay     = 4;
DWORD fpsLast         = 120;

// �Զ��������
DWORD lastMoveTime  = 0;
BOOL  keyDownA      = FALSE;
BOOL  keyDownD      = FALSE;
DWORD lastWheelTime = 0;
BOOL  wheelGate     = FALSE;
const DWORD WHEEL_GATE_WINDOW_MS = 200;
const DWORD STOP_THRESHOLD_MS     = 10;

// �����ļ�·��
wchar_t g_configPath[MAX_PATH] = L"";

// ǰ������
INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK LowLevelKbdProc(int, WPARAM, LPARAM);
LRESULT CALLBACK LowLevelMouseProc(int, WPARAM, LPARAM);

// ���ߺ���
void UpdateStatus(const wchar_t* text) {
    if (g_hWnd) SetDlgItemTextW(g_hWnd, IDC_STATIC_STATUS, text);
}
void PressKey(BYTE vk, BOOL isDown) {
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = isDown ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}
void SendWheelDown() {
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = -WHEEL_DELTA;
    SendInput(1, &in, sizeof(in));
}
void SendShift(BOOL down) {
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_SHIFT;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}
void MoveMouseRelative(int dx, int dy) {
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}
void PressReleaseKey(WORD vk, int delay) {
    INPUT in[2] = {0};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(delay);
    SendInput(1, &in[1], sizeof(INPUT));
}

// ����
void VkName(UINT vk, wchar_t* buf, size_t cch) {
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) scan |= 0x01000000;
    if (GetKeyNameTextW((LONG)scan, buf, (int)cch) <= 0) wsprintfW(buf, L"VK 0x%02X", vk);
}
void RefreshHotkeyEdits() {
    wchar_t buf[64];
    if (!g_hWnd) return;
    VkName(vkBhop,   buf, 64); SetDlgItemTextW(g_hWnd, IDC_EDIT_BHOP,   buf);
    VkName(vkCrouch, buf, 64); SetDlgItemTextW(g_hWnd, IDC_EDIT_CROUCH, buf);
    VkName(vkHasten, buf, 64); SetDlgItemTextW(g_hWnd, IDC_EDIT_HASTEN, buf);
    VkName(vkAutoAS, buf, 64); SetDlgItemTextW(g_hWnd, IDC_EDIT_AUTOAS, buf);
}

// RawInput
void RegisterRawInput(HWND hWnd) {
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage     = 0x02;
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = hWnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

// ����·��
void InitConfigPath() {
    wchar_t docPath[MAX_PATH] = L"";
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docPath))) {
        wsprintfW(g_configPath, L"%s\\HotkeyToolConfig.ini", docPath);
    } else {
        GetModuleFileNameW(NULL, g_configPath, MAX_PATH);
        wchar_t* p = wcsrchr(g_configPath, L'\\');
        if (p) *(p + 1) = L'\0';
        wcscat_s(g_configPath, MAX_PATH, L"HotkeyToolConfig.ini");
    }
}

// INI
void SaveConfig() {
    wchar_t buf[32];
    wsprintfW(buf, L"%u", vkBhop);   WritePrivateProfileStringW(L"Hotkeys", L"Bhop",   buf, g_configPath);
    wsprintfW(buf, L"%u", vkCrouch); WritePrivateProfileStringW(L"Hotkeys", L"Crouch", buf, g_configPath);
    wsprintfW(buf, L"%u", vkHasten); WritePrivateProfileStringW(L"Hotkeys", L"Hasten", buf, g_configPath);
    wsprintfW(buf, L"%u", vkAutoAS); WritePrivateProfileStringW(L"Hotkeys", L"AutoAS", buf, g_configPath);

    wsprintfW(buf, L"%d", bhopDelay);       WritePrivateProfileStringW(L"Params", L"BhopDelay",       buf, g_configPath);
    wsprintfW(buf, L"%d", crouchDown);      WritePrivateProfileStringW(L"Params", L"CrouchDown",      buf, g_configPath);
    wsprintfW(buf, L"%d", crouchUp);        WritePrivateProfileStringW(L"Params", L"CrouchUp",        buf, g_configPath);
    wsprintfW(buf, L"%d", hastenAmplitude); WritePrivateProfileStringW(L"Params", L"HastenAmplitude", buf, g_configPath);
    wsprintfW(buf, L"%d", hastenDelay);     WritePrivateProfileStringW(L"Params", L"HastenDelay",     buf, g_configPath);
}
void LoadConfig() {
    vkBhop   = GetPrivateProfileIntW(L"Hotkeys", L"Bhop",   VK_F6, g_configPath);
    vkCrouch = GetPrivateProfileIntW(L"Hotkeys", L"Crouch", VK_F7, g_configPath);
    vkHasten = GetPrivateProfileIntW(L"Hotkeys", L"Hasten", VK_F8, g_configPath);
    vkAutoAS = GetPrivateProfileIntW(L"Hotkeys", L"AutoAS", VK_F9, g_configPath);

    bhopDelay       = GetPrivateProfileIntW(L"Params", L"BhopDelay",       3,   g_configPath);
    crouchDown      = GetPrivateProfileIntW(L"Params", L"CrouchDown",      29,  g_configPath);
    crouchUp        = GetPrivateProfileIntW(L"Params", L"CrouchUp",        4,   g_configPath);
    hastenAmplitude = GetPrivateProfileIntW(L"Params", L"HastenAmplitude", 150, g_configPath);
    hastenDelay     = GetPrivateProfileIntW(L"Params", L"HastenDelay",     4,   g_configPath);
}

// ע��� FPS
DWORD ReadCurrentFPSFromRegistry() {
    HKEY hKey;
    DWORD fps = 120, type = REG_DWORD, size = sizeof(DWORD);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Wooduan\\�����ѻ�΢��ս��", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"FrameLimit_h731164557", NULL, &type, (LPBYTE)&fps, &size);
        RegCloseKey(hKey);
    }
    return fps;
}

// �߳�
DWORD WINAPI ThreadBhop(LPVOID) {
    timeBeginPeriod(1);
    while (running) {
        if (WaitForSingleObject(evtBhopStart, INFINITE) == WAIT_OBJECT_0) {
            UpdateStatus(L"����������...");
            while (bhopActive && running) { SendWheelDown(); Sleep(bhopDelay); }
            UpdateStatus(L"������ֹͣ");
        }
    }
    timeEndPeriod(1);
    return 0;
}
DWORD WINAPI ThreadCrouch(LPVOID) {
    timeBeginPeriod(1);
    while (running) {
        if (WaitForSingleObject(evtCrouchStart, INFINITE) == WAIT_OBJECT_0) {
            UpdateStatus(L"˫��������...");
            while (crouchHeld && running) { SendShift(TRUE); Sleep(crouchDown); SendShift(FALSE); Sleep(crouchUp); }
            UpdateStatus(L"˫����ֹͣ");
        }
    }
    timeEndPeriod(1);
    return 0;
}
DWORD WINAPI ThreadHasten(LPVOID) {
    timeBeginPeriod(1);
    while (running) {
        if (WaitForSingleObject(evtHastenStart, INFINITE) == WAIT_OBJECT_0) {
            UpdateStatus(L"���ٽű�������...");
            while (hastenHeld && running) {
                MoveMouseRelative(hastenAmplitude, 0);
                PressReleaseKey('A', hastenDelay);
                MoveMouseRelative(-hastenAmplitude, 0);
                PressReleaseKey('D', hastenDelay);
            }
            UpdateStatus(L"���ٽű���ֹͣ");
        }
    }
    timeEndPeriod(1);
    return 0;
}
DWORD WINAPI ThreadAutoAS(LPVOID) {
    timeBeginPeriod(1);
    while (running) {
        if (WaitForSingleObject(evtAutoASStart, INFINITE) == WAIT_OBJECT_0) {
            UpdateStatus(L"�Զ�����������...");
            while (autoASHeld && running) {
                DWORD now = GetTickCount();
                wheelGate = (now - lastWheelTime <= WHEEL_GATE_WINDOW_MS);
                if (!wheelGate || (now - lastMoveTime > STOP_THRESHOLD_MS)) {
                    if (keyDownA) { PressKey('A', FALSE); keyDownA = FALSE; }
                    if (keyDownD) { PressKey('D', FALSE); keyDownD = FALSE; }
                }
                Sleep(5);
            }
            if (keyDownA) { PressKey('A', FALSE); keyDownA = FALSE; }
            if (keyDownD) { PressKey('D', FALSE); keyDownD = FALSE; }
            UpdateStatus(L"�Զ�������ֹͣ");
        }
    }
    timeEndPeriod(1);
    return 0;
}

// ����
LRESULT CALLBACK LowLevelKbdProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        UINT vk = p->vkCode;
        if (wParam == WM_KEYDOWN) {
            if (capBhop)   { vkBhop   = vk; capBhop   = FALSE; RefreshHotkeyEdits(); UpdateStatus(L"�����ȼ�������"); return 1; }
            if (capCrouch) { vkCrouch = vk; capCrouch = FALSE; RefreshHotkeyEdits(); UpdateStatus(L"˫���ȼ�������"); return 1; }
            if (capHasten) { vkHasten = vk; capHasten = FALSE; RefreshHotkeyEdits(); UpdateStatus(L"���ٽű��ȼ�������"); return 1; }
            if (capAutoAS) { vkAutoAS = vk; capAutoAS = FALSE; RefreshHotkeyEdits(); UpdateStatus(L"�Զ������ȼ�������"); return 1; }
        }
        if (vk == vkBhop && wParam == WM_KEYDOWN) { bhopActive = !bhopActive; if (bhopActive) SetEvent(evtBhopStart); }
        if (vk == vkCrouch) { if (wParam == WM_KEYDOWN && !crouchHeld) { crouchHeld = TRUE; SetEvent(evtCrouchStart); } else if (wParam == WM_KEYUP) { crouchHeld = FALSE; } }
        if (vk == vkHasten) { if (wParam == WM_KEYDOWN && !hastenHeld) { hastenHeld = TRUE; SetEvent(evtHastenStart); } else if (wParam == WM_KEYUP) { hastenHeld = FALSE; } }
        if (vk == vkAutoAS) { if (wParam == WM_KEYDOWN && !autoASHeld) { autoASHeld = TRUE; SetEvent(evtAutoASStart); } else if (wParam == WM_KEYUP) { autoASHeld = FALSE; } }
    }
    return CallNextHookEx(hKbdHook, nCode, wParam, lParam);
}
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_MOUSEWHEEL) {
            SHORT zDelta = (SHORT)HIWORD(p->mouseData);
            if (zDelta < 0) { lastWheelTime = GetTickCount(); wheelGate = TRUE; }
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

// �Ի������
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_hWnd = hDlg;

        // �����ʼ��
        INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(icex); icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);

        // RawInput ע��
        RegisterRawInput(hDlg);

        // ���ô��ڱ��⣨���ģ�
        SetWindowTextW(hDlg, L"�����ѻ�������  By : Reinwi_Ternence");

        // ���ı�ǩ�밴ť�ı�ͳһ���ã�������Դ���뵼������
        SetDlgItemTextW(hDlg, IDC_STATIC_BHOP_LABEL,         L"�����ȼ�:");
        SetDlgItemTextW(hDlg, IDC_STATIC_BHOP_DELAY_LABEL,   L"�ӳ�(ms):");
        SetDlgItemTextW(hDlg, IDC_STATIC_CROUCH_LABEL,       L"˫���ȼ�:");
        SetDlgItemTextW(hDlg, IDC_STATIC_CROUCH_DOWN_LABEL,  L"����(ms):");
        SetDlgItemTextW(hDlg, IDC_STATIC_CROUCH_UP_LABEL,    L"�ɿ�(ms):");
        SetDlgItemTextW(hDlg, IDC_STATIC_HASTEN_LABEL,       L"���ٽű��ȼ�:");
        SetDlgItemTextW(hDlg, IDC_STATIC_HASTEN_AMP_LABEL,   L"����:");
        SetDlgItemTextW(hDlg, IDC_STATIC_HASTEN_DELAY_LABEL, L"�ӳ�(ms):");
        SetDlgItemTextW(hDlg, IDC_STATIC_AUTOAS_LABEL,       L"�Զ������ȼ�:");
        SetDlgItemTextW(hDlg, IDC_STATIC_FPS_LABEL,          L"��֡:");
        SetDlgItemTextW(hDlg, IDC_STATIC_STATUS,             L"״̬: ����");

        SetDlgItemTextW(hDlg, IDC_BTN_FPS,   L"��֡");
        SetDlgItemTextW(hDlg, IDC_BTN_APPLY, L"Ӧ��");

        // ��ʼ���ȼ���ʾ�����
        RefreshHotkeyEdits();
        wchar_t buf[32];
        wsprintfW(buf, L"%d", bhopDelay);        SetDlgItemTextW(hDlg, IDC_EDIT_BHOP_DELAY, buf);
        wsprintfW(buf, L"%d", crouchDown);       SetDlgItemTextW(hDlg, IDC_EDIT_CROUCH_DOWN, buf);
        wsprintfW(buf, L"%d", crouchUp);         SetDlgItemTextW(hDlg, IDC_EDIT_CROUCH_UP, buf);
        wsprintfW(buf, L"%d", hastenAmplitude);  SetDlgItemTextW(hDlg, IDC_EDIT_HASTEN_AMP, buf);
        wsprintfW(buf, L"%d", hastenDelay);      SetDlgItemTextW(hDlg, IDC_EDIT_HASTEN_DELAY, buf);

        // ��ֵ֡
        fpsLast = ReadCurrentFPSFromRegistry();
        wsprintfW(buf, L"%u", fpsLast);
        SetDlgItemTextW(hDlg, IDC_EDIT_FPS, buf);

        // Ӧ��ͼ�� app.ico
        HICON hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCE(IDI_APPICON),
                                        IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        if (hIcon) {
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessageW(hDlg, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        }

        // ����༭���Զ���ý��㵼�½���¼��ģʽ
        SetFocus(GetDlgItem(hDlg, IDC_BTN_APPLY));
        return FALSE; // �ֶ������˽���
    }
    case WM_INPUT: {
        UINT dwSize = sizeof(RAWINPUT);
        static BYTE lpb[sizeof(RAWINPUT)];
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) == sizeof(RAWINPUT)) {
            RAWINPUT* raw = (RAWINPUT*)lpb;
            if (raw->header.dwType == RIM_TYPEMOUSE) {
                int dx = raw->data.mouse.lLastX;
                DWORD now = GetTickCount();
                if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                    SHORT zDelta = (SHORT)raw->data.mouse.usButtonData;
                    if (zDelta < 0) { lastWheelTime = now; wheelGate = TRUE; }
                }
                if (autoASHeld) {
                    lastMoveTime = now;
                    if (wheelGate && (now - lastWheelTime <= WHEEL_GATE_WINDOW_MS)) {
                        if (dx < 0) {
                            if (!keyDownA) { PressKey('A', TRUE); keyDownA = TRUE; }
                            if (keyDownD) { PressKey('D', FALSE); keyDownD = FALSE; }
                        } else if (dx > 0) {
                            if (!keyDownD) { PressKey('D', TRUE); keyDownD = TRUE; }
                            if (keyDownA) { PressKey('A', FALSE); keyDownA = FALSE; }
                        }
                    }
                }
            }
        }
        return TRUE;
    }
    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_SETFOCUS) {
            switch (LOWORD(wParam)) {
            case IDC_EDIT_BHOP:   capBhop   = TRUE; UpdateStatus(L"�밴��һ������Ϊ�����ȼ�"); break;
            case IDC_EDIT_CROUCH: capCrouch = TRUE; UpdateStatus(L"�밴��һ������Ϊ˫���ȼ�"); break;
            case IDC_EDIT_HASTEN: capHasten = TRUE; UpdateStatus(L"�밴��һ������Ϊ���ٽű��ȼ�"); break;
            case IDC_EDIT_AUTOAS: capAutoAS = TRUE; UpdateStatus(L"�밴��һ������Ϊ�Զ������ȼ�"); break;
            }
        }
        switch (LOWORD(wParam)) {
        case IDC_BTN_APPLY: {
            wchar_t buf[32];
            GetDlgItemTextW(hDlg, IDC_EDIT_BHOP_DELAY, buf, 32);   bhopDelay       = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_EDIT_CROUCH_DOWN, buf, 32);  crouchDown      = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_EDIT_CROUCH_UP, buf, 32);    crouchUp        = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_EDIT_HASTEN_AMP, buf, 32);   hastenAmplitude = _wtoi(buf);
            GetDlgItemTextW(hDlg, IDC_EDIT_HASTEN_DELAY, buf, 32); hastenDelay     = _wtoi(buf);
            SaveConfig();
            UpdateStatus(L"�������ȼ��Ѹ��²�����");
            return TRUE;
        }
        case IDC_BTN_FPS: {
            wchar_t buf[32];
            GetDlgItemTextW(hDlg, IDC_EDIT_FPS, buf, 32);
            DWORD fps = _wtoi(buf);
            fpsLast = fps;

            HKEY hKey;
            LONG res = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Wooduan\\�����ѻ�΢��ս��",
                                       0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
            if (res == ERROR_SUCCESS) {
                RegSetValueExW(hKey, L"FrameLimit_h731164557", 0, REG_DWORD,
                               (const BYTE*)&fps, sizeof(DWORD));
                RegCloseKey(hKey);
                UpdateStatus(L"��֡�ɹ�");
            } else {
                UpdateStatus(L"��֡ʧ��");
            }
            return TRUE;
        }
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return TRUE;
    }
    return FALSE;
}

// �������
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_hInst = hInstance;

    InitConfigPath();
    LoadConfig();

    hKbdHook   = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbdProc, hInstance, 0);
    hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

    evtBhopStart   = CreateEventW(NULL, FALSE, FALSE, NULL);
    evtCrouchStart = CreateEventW(NULL, FALSE, FALSE, NULL);
    evtHastenStart = CreateEventW(NULL, FALSE, FALSE, NULL);
    evtAutoASStart = CreateEventW(NULL, FALSE, FALSE, NULL);

    CreateThread(NULL, 0, ThreadBhop,   NULL, 0, NULL);
    CreateThread(NULL, 0, ThreadCrouch, NULL, 0, NULL);
    CreateThread(NULL, 0, ThreadHasten, NULL, 0, NULL);
    CreateThread(NULL, 0, ThreadAutoAS, NULL, 0, NULL);

    DialogBoxParamW(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc, 0);

    running = FALSE;
    if (hKbdHook)   UnhookWindowsHookEx(hKbdHook);
    if (hMouseHook) UnhookWindowsHookEx(hMouseHook);
    if (evtBhopStart)   CloseHandle(evtBhopStart);
    if (evtCrouchStart) CloseHandle(evtCrouchStart);
    if (evtHastenStart) CloseHandle(evtHastenStart);
    if (evtAutoASStart) CloseHandle(evtAutoASStart);

    return 0;
}
