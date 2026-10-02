// =============================================================================
//  Lab_view — Przeglądarka plików graficznych / Image viewer companion
//  Wersja / Version: 2.5
//  Autor / Author:   Maciej Sikorski
//  Data / Date:      01.04.2026
//
//  Towarzysz LabSim — zaawansowana przeglądarka zdjęć z obsługą RAW, TIFF, JPG
//  LabSim companion — advanced image viewer with RAW, TIFF, JPG support
//
// =============================================================================
//  LICENCJA / LICENSE — Apache 2.0
// =============================================================================
//
//  Copyright (c) 2026 Maciej Sikorski
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//
// =============================================================================
//  HISTORIA ZMIAN / CHANGELOG
// =============================================================================
//   cpp-12  canvas skalujący + poprawny layout podglądu
//   cpp-13  - podgląd (duży obraz) ładowany asynchronicznie w wątku tła
//           - usunięto podwójne "oprawianie w ramkę" dużego podglądu
//           - okno podglądu spójnie ciemne od krawędzi do krawędzi
//           - nowy panel "Informacje o pliku" pod drzewem folderów
//   cpp-14  - Poprawka #1: wyciek pamięci w InsertTreeNode (TVN_DELETEITEM)
//           - Poprawka #2: logika wysokości drzewa (LayoutMainWindow)
//           - Poprawka #3: bezpieczne zamykanie wątków (g_shuttingDown + join)
//           - Poprawka #4: zwolnienie t_wicFactory w wątkach roboczych
//           - Poprawka #5: fallback ciemnego motywu (DWMWA 20 -> 19)
//           - Okno "O programie" (F1) z pełną informacją o wersji i buildzie
//           - Osobne okno "Skróty klawiszowe" (F2)
//           - Menu główne: Plik / Pomoc
//   cpp-15  - Tłumaczenia z plików languages\*.json (pl / en / it, łatwo dodać kolejne)
//           - Menu "Język" — przełączanie języka w locie, zapamiętane w ustawieniach
//           - Okna "O programie" i "Skróty" w jednym, wybranym języku
// =============================================================================

#define _WIN32_WINNT 0x0601
#define NTDDI_VERSION 0x06010000
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_IE 0x0600

#include <sdkddkver.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <deque>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <initializer_list>
#include <cwctype>
#include <cstdio>

#include "resource.h" // IDI_APPICON — patrz Lab_view.rc / lab_view.ico

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

// =============================================================================
// POPRAWKA #5: FALLBACK DLA CIEMNEGO MOTYWU / DARK MODE FALLBACK
// Windows 10 1809–1903 używa atrybutu 19, Win11 / Win10 21H2+ atrybutu 20.
// =============================================================================
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

// =============================================================================
// POPRAWKA #3: FLAGA BEZPIECZNEGO ZAMYKANIA / THREAD-SAFE SHUTDOWN FLAG
// =============================================================================
static std::atomic<bool> g_shuttingDown(false);

// =============================================================================
// ROZSZERZENIA / EXTENSIONS
// =============================================================================
static const wchar_t* LABSIM_RAW_EXT[]  = { L".nef", L".raf" };
static const wchar_t* LABSIM_TIFF_EXT[] = { L".tif", L".tiff" };
static const wchar_t* OTHER_RAW_EXT[]   = { L".cr2", L".cr3", L".arw",
                                            L".dng", L".rw2", L".orf", L".raw" };
static const wchar_t* JPG_EXT[]         = { L".jpg", L".jpeg" };
static const wchar_t* ALL_IMAGE_EXT[]   = {
    L".nef", L".raf", L".tif", L".tiff",
    L".cr2", L".cr3", L".arw", L".dng", L".rw2", L".orf", L".raw",
    L".jpg", L".jpeg"
};

// =============================================================================
// WYMIARY I STAŁE / DIMENSIONS AND CONSTANTS
// =============================================================================
static const int THUMB_W = 128;
static const int THUMB_H = 128;
static const int ICON_SPACING_X = 180;
static const int ICON_SPACING_Y = 200;
static const int PREVIEW_MAX_W = 2400;
static const int PREVIEW_MAX_H = 1800;
static const int META_PANEL_H = 190;
static const int META_HEADER_H = 22;

static const wchar_t* APP_VERSION = L"2.5";
static const wchar_t* APP_BUILD   = L"2026-cpp-15";
static const wchar_t* APP_AUTHOR  = L"Maciej Sikorski";
static const wchar_t* APP_DATE    = L"01.04.2026";

static const int ID_LIST = 1000;
static const int ID_TREE = 1001;
static const int ID_SPLIT = 1002;
static const int ID_SORT_COMBO = 1100;
static const int ID_FILTER_COMBO = 1101;
static const int ID_REFRESH_BTN = 1102;
static const int ID_BROWSE_BTN = 1103;
static const int ID_STATUSBAR = 1110;
static const int ID_PREVIEW_PREV = 1200;
static const int ID_PREVIEW_NEXT = 1201;
static const int ID_PREVIEW_EDIT = 1202;
static const int ID_PREVIEW_CLOSE = 1203;
static const int ID_PREVIEW_CANVAS = 1204;
static const int ID_META_HEADER = 1300;
static const int ID_META_PANEL = 1301;

// ID komend menu/skrótów (WM_COMMAND)
static const int IDM_ABOUT          = 1400;
static const int IDM_OPEN_LABSIM    = 1401;
static const int IDM_SORT_NAME      = 1402;
static const int IDM_SORT_DATE      = 1403;
static const int IDM_SORT_SIZE      = 1404;
static const int IDM_FILTER_ALL     = 1405;
static const int IDM_FILTER_RAW     = 1406;
static const int IDM_FILTER_JPG     = 1407;
static const int IDM_FILTER_TIFF    = 1408;
static const int IDM_SHORTCUTS      = 1409;
static const int IDM_EXIT           = 1410;
static const int IDM_LANG_BASE      = 1500;   // 1500..1599 = kolejne języki z folderu languages

static const UINT WM_APP_THUMB_READY   = WM_APP + 1;
static const UINT WM_APP_PREVIEW_READY = WM_APP + 2;

static const int MARGIN = 10;
static const int TOOLBAR_H = 44;
static const int STATUS_H = 24;

// =============================================================================
// SKRÓTY KLAWISZOWE / KEYBOARD SHORTCUTS
// =============================================================================
struct KeyboardShortcut {
    const wchar_t* keys;     // tekst klawiszy (niezależny od języka)
    const wchar_t* trKey;    // klucz tłumaczenia w languages\*.json
};

static const KeyboardShortcut SHORTCUTS[] = {
    { L"F1",      L"viewer.shortcut.about" },
    { L"F2",      L"viewer.shortcut.shortcuts" },
    { L"F5",      L"viewer.shortcut.refresh" },
    { L"Space",   L"viewer.shortcut.preview" },
    { L"Enter",   L"viewer.shortcut.open" },
    { L"Esc",     L"viewer.shortcut.close_preview" },
    { L"← / →",   L"viewer.shortcut.navigate" },
    { L"Ctrl+O",  L"viewer.shortcut.choose_folder" },
    { L"Ctrl+L",  L"viewer.shortcut.open_labsim" },
    { L"Ctrl+S",  L"viewer.shortcut.sort_name" },
    { L"Ctrl+D",  L"viewer.shortcut.sort_date" },
    { L"Ctrl+Z",  L"viewer.shortcut.sort_size" },
    { L"Ctrl+A",  L"viewer.shortcut.show_all" },
    { L"Ctrl+1",  L"viewer.shortcut.filter_raw" },
    { L"Ctrl+2",  L"viewer.shortcut.filter_jpg" },
    { L"Ctrl+3",  L"viewer.shortcut.filter_tiff" },
};

// =============================================================================
// GLOBALNY FONT
// =============================================================================
static HFONT g_hFont = nullptr;

static void InitSystemFont() {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    } else {
        g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
}

static void ApplyFontToWindow(HWND hwnd) {
    if (!hwnd || !g_hFont) return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
}

// =============================================================================
// Helpers
// =============================================================================
static std::wstring ToLower(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)std::towlower(c);
    return r;
}

static std::wstring GetExt(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    if (!ext) return L"";
    return ToLower(ext);
}

static bool ExtInList(const std::wstring& ext, const wchar_t** list, int n) {
    for (int i = 0; i < n; ++i) if (ext == list[i]) return true;
    return false;
}

static bool IsLabsimRaw(const std::wstring& p) {
    return ExtInList(GetExt(p), LABSIM_RAW_EXT,
                     sizeof(LABSIM_RAW_EXT) / sizeof(*LABSIM_RAW_EXT));
}
static bool IsLabsimTiff(const std::wstring& p) {
    return ExtInList(GetExt(p), LABSIM_TIFF_EXT,
                     sizeof(LABSIM_TIFF_EXT) / sizeof(*LABSIM_TIFF_EXT));
}
static bool IsLabsimOpenable(const std::wstring& p) {
    return IsLabsimRaw(p) || IsLabsimTiff(p);
}
static bool IsOtherRaw(const std::wstring& p) {
    return ExtInList(GetExt(p), OTHER_RAW_EXT,
                     sizeof(OTHER_RAW_EXT) / sizeof(*OTHER_RAW_EXT));
}
static bool IsJpg(const std::wstring& p) {
    return ExtInList(GetExt(p), JPG_EXT,
                     sizeof(JPG_EXT) / sizeof(*JPG_EXT));
}
static bool IsAnyImage(const std::wstring& p) {
    return ExtInList(GetExt(p), ALL_IMAGE_EXT,
                     sizeof(ALL_IMAGE_EXT) / sizeof(*ALL_IMAGE_EXT));
}
static bool IsAnyRaw(const std::wstring& p) {
    return IsLabsimRaw(p) || IsOtherRaw(p);
}

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return buf;
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

// =============================================================================
// USTAWIENIA / SETTINGS
// =============================================================================
struct Settings {
    std::wstring sortMode   = L"name";
    std::wstring filterMode = L"all";
    std::wstring lastFolder;
    std::wstring language;          // kod języka, np. "pl" (puste = wykryj z systemu)
    int          sashPos    = 320;
};

static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) {
        if (c == L'\\') r += L"\\\\";
        else if (c == L'"') r += L"\\\"";
        else r += c;
    }
    return r;
}

static std::wstring JsonUnescape(const std::wstring& s) {
    std::wstring r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') r += L'\n';
            else if (n == L't') r += L'\t';
            else r += n;
        } else r += s[i];
    }
    return r;
}

static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a]==L' '||s[a]==L'\t'||s[a]==L'\r'||s[a]==L'\n')) ++a;
    while (b > a && (s[b-1]==L' '||s[b-1]==L'\t'||s[b-1]==L'\r'||s[b-1]==L'\n')) --b;
    return s.substr(a, b-a);
}

static Settings LoadSettings(const std::wstring& path) {
    Settings st;
    std::wifstream f(path);
    if (!f.is_open()) return st;
    std::wstring line;
    while (std::getline(f, line)) {
        line = TrimW(line);
        if (line.empty() || line == L"{" || line == L"}") continue;
        size_t q1 = line.find(L'"');
        if (q1 == std::wstring::npos) continue;
        size_t q2 = line.find(L'"', q1 + 1);
        if (q2 == std::wstring::npos) continue;
        std::wstring key = line.substr(q1 + 1, q2 - q1 - 1);
        size_t colon = line.find(L':', q2);
        if (colon == std::wstring::npos) continue;
        std::wstring v = TrimW(line.substr(colon + 1));
        if (!v.empty() && v.back() == L',') { v.pop_back(); v = TrimW(v); }
        if (!v.empty() && v.front() == L'"') {
            size_t end = v.find_last_of(L'"');
            if (end == 0 || end == std::wstring::npos) continue;
            std::wstring val = JsonUnescape(v.substr(1, end - 1));
            if (key == L"sort_mode")   st.sortMode   = val;
            else if (key == L"filter_mode") st.filterMode = val;
            else if (key == L"last_folder") st.lastFolder = val;
            else if (key == L"language")    st.language   = val;
        } else {
            try {
                int n = std::stoi(v);
                if (key == L"sash_pos") st.sashPos = n;
            } catch (...) {}
        }
    }
    return st;
}

static void SaveSettings(const std::wstring& path, const Settings& st) {
    std::wofstream f(path);
    if (!f.is_open()) return;
    f << L"{\n"
      << L"  \"sort_mode\": \""   << JsonEscape(st.sortMode)   << L"\",\n"
      << L"  \"filter_mode\": \"" << JsonEscape(st.filterMode) << L"\",\n"
      << L"  \"last_folder\": \"" << JsonEscape(st.lastFolder) << L"\",\n"
      << L"  \"language\": \""    << JsonEscape(st.language)    << L"\",\n"
      << L"  \"sash_pos\": "      << st.sashPos                 << L"\n"
      << L"}\n";
}

// =============================================================================
// TŁUMACZENIA / I18N
// Pliki: <folder exe>\languages\<kod>.json  (np. pl.json, en.json, it.json)
// Format: płaski JSON "klucz": "tekst". Nazwa języka w menu pochodzi z _meta.name.
// Aby dodać język wystarczy wrzucić nowy plik .json do folderu languages.
// Brakujący klucz -> bierzemy z en.json -> w ostateczności pokazujemy sam klucz.
// =============================================================================
struct LangInfo { std::wstring code; std::wstring name; };

static std::map<std::wstring, std::wstring> g_tr;          // aktualny język
static std::map<std::wstring, std::wstring> g_trFallback;  // en (zapasowy)
static std::vector<LangInfo> g_langs;
static std::wstring g_langCode;
static std::wstring g_langDir;

static bool ReadUtf8File(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD rd = 0;
    BOOL ok = buf.empty() ? TRUE : ReadFile(h, &buf[0], (DWORD)buf.size(), &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(rd);
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) off = 3;
    int n = MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off), nullptr, 0);
    out.assign(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, buf.data() + off, (int)(buf.size() - off), &out[0], n);
    return true;
}

// Prosty parser JSON: obiekt ze stringami (zagnieżdżone obiekty -> klucz "a.b").
struct JsonFlatParser {
    const std::wstring& s;
    size_t i = 0;
    explicit JsonFlatParser(const std::wstring& str) : s(str) {}

    void ws() {
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) ++i;
    }
    bool str(std::wstring& r) {
        if (i >= s.size() || s[i] != L'"') return false;
        ++i;
        r.clear();
        while (i < s.size()) {
            wchar_t c = s[i++];
            if (c == L'"') return true;
            if (c == L'\\' && i < s.size()) {
                wchar_t e = s[i++];
                switch (e) {
                    case L'n': r += L'\n'; break;
                    case L't': r += L'\t'; break;
                    case L'r': r += L'\r'; break;
                    case L'b': r += L'\b'; break;
                    case L'f': r += L'\f'; break;
                    case L'u':
                        if (i + 4 <= s.size()) {
                            r += (wchar_t)wcstoul(s.substr(i, 4).c_str(), nullptr, 16);
                            i += 4;
                        }
                        break;
                    default: r += e; break;   // \" \\ \/
                }
            } else {
                r += c;
            }
        }
        return false;
    }
    bool object(const std::wstring& prefix, std::map<std::wstring, std::wstring>& out) {
        ws();
        if (i >= s.size() || s[i] != L'{') return false;
        ++i;
        for (;;) {
            ws();
            if (i >= s.size()) return false;
            if (s[i] == L'}') { ++i; return true; }
            if (s[i] == L',') { ++i; continue; }
            std::wstring k;
            if (!str(k)) return false;
            ws();
            if (i >= s.size() || s[i] != L':') return false;
            ++i;
            ws();
            if (i >= s.size()) return false;
            std::wstring full = prefix.empty() ? k : prefix + L"." + k;
            if (s[i] == L'"') {
                std::wstring v;
                if (!str(v)) return false;
                out[full] = v;
            } else if (s[i] == L'{') {
                if (!object(full, out)) return false;
            } else {
                while (i < s.size() && s[i] != L',' && s[i] != L'}') ++i;   // liczby / bool / null — pomijamy
            }
        }
    }
};

static bool LoadLangFile(const std::wstring& code, std::map<std::wstring, std::wstring>& out) {
    std::wstring txt;
    if (!ReadUtf8File(JoinPath(g_langDir, code + L".json"), txt)) return false;
    std::map<std::wstring, std::wstring> tmp;
    JsonFlatParser p(txt);
    if (!p.object(L"", tmp)) return false;
    out.swap(tmp);
    return true;
}

static bool HasLang(const std::wstring& code) {
    for (auto& l : g_langs) if (_wcsicmp(l.code.c_str(), code.c_str()) == 0) return true;
    return false;
}

static void ScanLanguages() {
    g_langs.clear();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(g_langDir, L"*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring fn = fd.cFileName;
        if (fn.size() <= 5) continue;
        std::wstring code = fn.substr(0, fn.size() - 5);      // bez ".json"
        std::map<std::wstring, std::wstring> m;
        if (!LoadLangFile(code, m)) continue;
        LangInfo li;
        li.code = code;
        auto it = m.find(L"_meta.name");
        li.name = (it != m.end() && !it->second.empty()) ? it->second : code;
        g_langs.push_back(li);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(g_langs.begin(), g_langs.end(),
              [](const LangInfo& a, const LangInfo& b) { return _wcsicmp(a.code.c_str(), b.code.c_str()) < 0; });
}

static std::wstring DetectSystemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_POLISH:  return L"pl";
        case LANG_ITALIAN: return L"it";
        default:           return L"en";
    }
}

static void SetLanguageCode(const std::wstring& code) {
    std::map<std::wstring, std::wstring> m;
    if (LoadLangFile(code, m)) {
        g_tr.swap(m);
        g_langCode = code;
    }
}

// Zwraca tłumaczenie klucza (aktualny język -> en -> sam klucz).
static std::wstring Tr(const wchar_t* key) {
    auto it = g_tr.find(key);
    if (it != g_tr.end()) return it->second;
    it = g_trFallback.find(key);
    if (it != g_trFallback.end()) return it->second;
    return key;
}

// Tłumaczenie z podstawieniem {nazwa} -> wartość.
static std::wstring TrF(const wchar_t* key,
                        std::initializer_list<std::pair<const wchar_t*, std::wstring>> args) {
    std::wstring s = Tr(key);
    for (auto& a : args) {
        std::wstring ph = L"{" + std::wstring(a.first) + L"}";
        size_t pos = 0;
        while ((pos = s.find(ph, pos)) != std::wstring::npos) {
            s.replace(pos, ph.size(), a.second);
            pos += a.second.size();
        }
    }
    return s;
}

static void InitLanguages(const std::wstring& exeDir, const std::wstring& preferred) {
    g_langDir = JoinPath(exeDir, L"languages");
    ScanLanguages();
    if (g_langs.empty()) {
        MessageBoxW(nullptr,
            L"Nie znaleziono plików językowych w folderze:\n"
            L"Language files not found in folder:\n\n"
            L"languages\\ (pl.json, en.json, it.json)",
            L"Lab View", MB_OK | MB_ICONWARNING);
        return;
    }
    LoadLangFile(L"en", g_trFallback);
    std::wstring want = preferred;
    if (want.empty() || !HasLang(want)) want = DetectSystemLanguage();
    if (!HasLang(want)) want = HasLang(L"en") ? L"en" : g_langs[0].code;
    SetLanguageCode(want);
}

// =============================================================================
// GDI+
// =============================================================================
static ULONG_PTR g_gdiplusToken = 0;

static void InitGdiplus() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &si, nullptr);
}

static void ShutdownGdiplus() {
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

// =============================================================================
// WIC + POPRAWKA #4: ZWOLNIENIE t_wicFactory
// =============================================================================
static thread_local IWICImagingFactory* t_wicFactory = nullptr;

static bool InitWIC() {
    if (t_wicFactory) return true;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t_wicFactory));
    return SUCCEEDED(hr) && t_wicFactory;
}

// POPRAWKA #4: właściwe zwolnienie WIC w wątku / proper WIC cleanup per thread
static void ShutdownWIC() {
    if (t_wicFactory) {
        t_wicFactory->Release();
        t_wicFactory = nullptr;
    }
}

static UINT ReadExifOrientation(IWICBitmapFrameDecode* frame) {
    IWICMetadataQueryReader* qr = nullptr;
    if (FAILED(frame->GetMetadataQueryReader(&qr)) || !qr) return 0;
    PROPVARIANT v; PropVariantInit(&v);
    UINT orient = 0;
    if (SUCCEEDED(qr->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v))) {
        if (v.vt == VT_UI2) orient = v.uiVal;
        else if (v.vt == VT_UI4) orient = v.ulVal;
    }
    PropVariantClear(&v);
    qr->Release();
    return orient;
}

// =============================================================================
// POPRAWKA #5: CIEMNY MOTYW Z FALLBACKIEM / DARK MODE WITH FALLBACK
// =============================================================================
static bool IsSystemDarkMode() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD val = 1, size = sizeof(val);
    LSTATUS st = RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                                  (LPBYTE)&val, &size);
    RegCloseKey(hKey);
    return st == ERROR_SUCCESS && val == 0;
}

static void ApplyDarkMode(HWND hwnd, bool dark) {
    if (!hwnd) return;
    BOOL v = dark ? TRUE : FALSE;
    // Atrybut 20: Win11 / Win10 21H2+
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &v, sizeof(v)))) return;
    // Atrybut 19: Win10 1809–1903
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &v, sizeof(v)))) return;
}

// =============================================================================
// BITMAP HELPERS
// =============================================================================
static HBITMAP LetterboxTo(HBITMAP src, int dstW, int dstH) {
    if (!src) return nullptr;
    BITMAP bm = {};
    if (!GetObject(src, sizeof(bm), &bm) || bm.bmWidth == 0 || bm.bmHeight == 0)
        return nullptr;

    int srcW = bm.bmWidth;
    int srcH = bm.bmHeight;

    double sx = (double)dstW / srcW;
    double sy = (double)dstH / srcH;
    double sc = (sx < sy) ? sx : sy;
    int newW = (int)(srcW * sc + 0.5);
    int newH = (int)(srcH * sc + 0.5);
    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;
    if (newW > dstW) newW = dstW;
    if (newH > dstH) newH = dstH;
    int offX = (dstW - newW) / 2;
    int offY = (dstH - newH) / 2;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dstW;
    bmi.bmiHeader.biHeight = -dstH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dst = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dst) return nullptr;

    HDC hdcDst = CreateCompatibleDC(nullptr);
    HGDIOBJ oldDst = SelectObject(hdcDst, dst);

    RECT rcf = { 0, 0, dstW, dstH };
    FillRect(hdcDst, &rcf, (HBRUSH)GetStockObject(WHITE_BRUSH));

    HDC hdcSrc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldSrc = SelectObject(hdcSrc, src);

    SetStretchBltMode(hdcDst, HALFTONE);
    SetBrushOrgEx(hdcDst, 0, 0, nullptr);
    StretchBlt(hdcDst, offX, offY, newW, newH, hdcSrc, 0, 0, srcW, srcH, SRCCOPY);

    SelectObject(hdcSrc, oldSrc);
    SelectObject(hdcDst, oldDst);
    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);

    return dst;
}

static HBITMAP ScaleBitmapExact(HBITMAP src, int newW, int newH) {
    if (!src || newW < 1 || newH < 1) return nullptr;
    BITMAP bm = {};
    if (!GetObject(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = newW;
    bmi.bmiHeader.biHeight = -newH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dst = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dst) return nullptr;

    HDC hdcDst = CreateCompatibleDC(nullptr);
    HGDIOBJ oldDst = SelectObject(hdcDst, dst);
    HDC hdcSrc = CreateCompatibleDC(nullptr);
    HGDIOBJ oldSrc = SelectObject(hdcSrc, src);

    SetStretchBltMode(hdcDst, HALFTONE);
    SetBrushOrgEx(hdcDst, 0, 0, nullptr);
    StretchBlt(hdcDst, 0, 0, newW, newH, hdcSrc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

    SelectObject(hdcSrc, oldSrc);
    SelectObject(hdcDst, oldDst);
    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);
    return dst;
}

static HBITMAP WICFrameToHBITMAP(IWICBitmapSource* src) {
    if (!src || !t_wicFactory) return nullptr;
    UINT srcW = 0, srcH = 0;
    src->GetSize(&srcW, &srcH);
    if (srcW == 0 || srcH == 0) return nullptr;

    IWICFormatConverter* conv = nullptr;
    if (FAILED(t_wicFactory->CreateFormatConverter(&conv)) || !conv) return nullptr;
    HRESULT hr = conv->Initialize(src, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { conv->Release(); return nullptr; }

    UINT stride = srcW * 4;
    UINT bufSize = stride * srcH;
    std::vector<BYTE> buf(bufSize);
    hr = conv->CopyPixels(nullptr, stride, bufSize, buf.data());
    conv->Release();
    if (FAILED(hr)) return nullptr;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = srcW;
    bmi.bmiHeader.biHeight = -((LONG)srcH);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp || !bits) { if (hbmp) DeleteObject(hbmp); return nullptr; }
    memcpy(bits, buf.data(), bufSize);
    return hbmp;
}

static HBITMAP LoadViaWIC(const std::wstring& path, bool allowThumb) {
    if (!InitWIC()) return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = t_wicFactory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) return nullptr;

    if (allowThumb) {
        IWICBitmapSource* thumb = nullptr;
        if (SUCCEEDED(decoder->GetThumbnail(&thumb)) && thumb) {
            HBITMAP b = WICFrameToHBITMAP(thumb);
            thumb->Release();
            if (b) { decoder->Release(); return b; }
        }
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { decoder->Release(); return nullptr; }

    UINT orient = ReadExifOrientation(frame);

    IWICBitmapSource* toUse = frame;
    IWICBitmapFlipRotator* rot = nullptr;
    if (orient >= 2 && orient <= 8) {
        WICBitmapTransformOptions opt = WICBitmapTransformRotate0;
        switch (orient) {
            case 2: opt = WICBitmapTransformFlipHorizontal; break;
            case 3: opt = WICBitmapTransformRotate180; break;
            case 4: opt = WICBitmapTransformFlipVertical; break;
            case 5: opt = (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
            case 6: opt = WICBitmapTransformRotate90; break;
            case 7: opt = (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
            case 8: opt = WICBitmapTransformRotate270; break;
        }
        if (SUCCEEDED(t_wicFactory->CreateBitmapFlipRotator(&rot)) && rot) {
            if (SUCCEEDED(rot->Initialize(frame, opt))) toUse = rot;
            else { rot->Release(); rot = nullptr; }
        }
    }

    HBITMAP b = WICFrameToHBITMAP(toUse);
    if (rot) rot->Release();
    frame->Release();
    decoder->Release();
    return b;
}

static HBITMAP LoadViaGDI(const std::wstring& path) {
    if (!g_gdiplusToken) return nullptr;
    using namespace Gdiplus;

    Bitmap* src = Bitmap::FromFile(path.c_str(), FALSE);
    if (!src || src->GetLastStatus() != Ok) {
        if (src) delete src;
        return nullptr;
    }
    UINT srcW = src->GetWidth();
    UINT srcH = src->GetHeight();
    if (srcW == 0 || srcH == 0) { delete src; return nullptr; }

    Bitmap* dst = new Bitmap(srcW, srcH, PixelFormat32bppARGB);
    if (!dst || dst->GetLastStatus() != Ok) {
        if (dst) delete dst;
        delete src;
        return nullptr;
    }
    {
        Graphics g(dst);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.DrawImage(src, 0, 0, (INT)srcW, (INT)srcH);
    }

    HBITMAP hbmp = nullptr;
    BitmapData data;
    Rect rect(0, 0, (INT)srcW, (INT)srcH);
    if (dst->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) == Ok) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)srcW;
        bmi.bmiHeader.biHeight = -((LONG)srcH);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hbmp && bits) {
            for (UINT y = 0; y < srcH; ++y) {
                memcpy((BYTE*)bits + y * srcW * 4,
                       (BYTE*)data.Scan0 + y * data.Stride,
                       srcW * 4);
            }
        }
        dst->UnlockBits(&data);
    }
    delete dst;
    delete src;
    return hbmp;
}

static HBITMAP LoadFileIconFromShell(const std::wstring& path) {
    SHFILEINFOW sfi = {};
    DWORD_PTR ret = SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                                   SHGFI_ICON | SHGFI_LARGEICON);
    if (!ret || !sfi.hIcon) return nullptr;

    ICONINFO ii = {};
    if (!GetIconInfo(sfi.hIcon, &ii)) {
        DestroyIcon(sfi.hIcon);
        return nullptr;
    }
    BITMAP bmc = {};
    if (!GetObject(ii.hbmColor, sizeof(bmc), &bmc)) {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        DestroyIcon(sfi.hIcon);
        return nullptr;
    }
    int iconW = bmc.bmWidth;
    int iconH = bmc.bmHeight;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = iconW;
    bmi.bmiHeader.biHeight = -iconH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hbmp) {
        HDC hdcDst = CreateCompatibleDC(nullptr);
        HGDIOBJ oldDst = SelectObject(hdcDst, hbmp);
        RECT rc = { 0, 0, iconW, iconH };
        FillRect(hdcDst, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        DrawIconEx(hdcDst, 0, 0, sfi.hIcon, iconW, iconH, 0, nullptr, DI_NORMAL);
        SelectObject(hdcDst, oldDst);
        DeleteDC(hdcDst);
    }

    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    DestroyIcon(sfi.hIcon);
    return hbmp;
}

static HBITMAP MakeFallbackThumb(const std::wstring& label, int w, int h) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp) return nullptr;

    HDC hdc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, hbmp);

    RECT rc = { 0, 0, w, h };
    HBRUSH brBg = CreateSolidBrush(RGB(0xFA, 0xFA, 0xFA));
    FillRect(hdc, &rc, brBg);
    DeleteObject(brBg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xCC, 0xCC, 0xCC));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 12, 12, w - 12, h - 12);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0x88, 0x88, 0x88));
    HGDIOBJ oldF = SelectObject(hdc, g_hFont);
    DrawTextW(hdc, label.c_str(), -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    SelectObject(hdc, old);
    DeleteDC(hdc);
    return hbmp;
}

static HBITMAP LoadImageScaled(const std::wstring& path, int maxW, int maxH, bool pad = true) {
    HBITMAP raw = nullptr;

    if (!raw) raw = LoadViaWIC(path, /*allowThumb=*/true);
    if (!raw) raw = LoadViaWIC(path, /*allowThumb=*/false);
    if (!raw) raw = LoadViaGDI(path);
    if (!raw) raw = LoadFileIconFromShell(path);

    if (!raw) return nullptr;

    if (!pad) {
        BITMAP bm = {};
        if (GetObject(raw, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            if (bm.bmWidth <= maxW && bm.bmHeight <= maxH) return raw;
            double sx = (double)maxW / bm.bmWidth;
            double sy = (double)maxH / bm.bmHeight;
            double sc = (sx < sy) ? sx : sy;
            int nw = (int)(bm.bmWidth * sc + 0.5);
            int nh = (int)(bm.bmHeight * sc + 0.5);
            HBITMAP scaled = ScaleBitmapExact(raw, nw, nh);
            if (scaled) { DeleteObject(raw); return scaled; }
        }
        return raw;
    }

    HBITMAP boxed = LetterboxTo(raw, maxW, maxH);
    if (boxed) {
        DeleteObject(raw);
        return boxed;
    }
    return raw;
}

// =============================================================================
// METADANE OBRAZU — EXIF + dane Lab_sim
// =============================================================================
struct ImageMetaInfo {
    bool ok = false;
    std::wstring camera;
    std::wstring lens;
    std::wstring exposure;
    std::wstring dateTaken;
    std::wstring dimensions;
    std::wstring fileSize;
    std::wstring labsimInfo;
};

static std::wstring FormatFileSize(ULONGLONG bytes) {
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    wchar_t buf[64];
    if (u == 0) _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d %s", (int)v, units[u]);
    else _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f %s", v, units[u]);
    return buf;
}

static bool GetPropUInt(const PROPVARIANT& v, UINT& out) {
    switch (v.vt) {
        case VT_UI1: out = v.bVal; return true;
        case VT_UI2: out = v.uiVal; return true;
        case VT_UI4: out = v.ulVal; return true;
        case VT_I4:  out = (UINT)v.lVal; return true;
        default: return false;
    }
}

static bool GetPropRational(const PROPVARIANT& v, double& out) {
    if (v.vt == VT_UI8) {
        UINT num = (UINT)(v.uhVal.QuadPart & 0xFFFFFFFFull);
        UINT den = (UINT)((v.uhVal.QuadPart >> 32) & 0xFFFFFFFFull);
        if (den == 0) return false;
        out = (double)num / (double)den;
        return true;
    }
    if (v.vt == VT_R8) { out = v.dblVal; return true; }
    UINT u;
    if (GetPropUInt(v, u)) { out = (double)u; return true; }
    return false;
}

static bool GetPropString(const PROPVARIANT& v, std::wstring& out) {
    if (v.vt == VT_LPWSTR && v.pwszVal) { out = v.pwszVal; return true; }
    if (v.vt == VT_BSTR && v.bstrVal)   { out = v.bstrVal; return true; }
    if (v.vt == VT_LPSTR && v.pszVal) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, v.pszVal, -1, nullptr, 0);
        UINT cp = CP_UTF8;
        if (wlen <= 0) { wlen = MultiByteToWideChar(CP_ACP, 0, v.pszVal, -1, nullptr, 0); cp = CP_ACP; }
        if (wlen > 0) {
            std::vector<wchar_t> buf(wlen);
            MultiByteToWideChar(cp, 0, v.pszVal, -1, buf.data(), wlen);
            out = buf.data();
        }
        return !out.empty();
    }
    return false;
}

static bool TryGetString(IWICMetadataQueryReader* qr,
                         std::initializer_list<const wchar_t*> paths, std::wstring& out) {
    for (auto p : paths) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(qr->GetMetadataByName(p, &v))) {
            std::wstring s;
            bool got = GetPropString(v, s);
            PropVariantClear(&v);
            if (got) {
                while (!s.empty() && (s.back() == L'\0' || s.back() == L' ')) s.pop_back();
                if (!s.empty()) { out = s; return true; }
            }
        } else {
            PropVariantClear(&v);
        }
    }
    return false;
}

static bool TryGetRational(IWICMetadataQueryReader* qr,
                           std::initializer_list<const wchar_t*> paths, double& out) {
    for (auto p : paths) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(qr->GetMetadataByName(p, &v))) {
            bool got = GetPropRational(v, out);
            PropVariantClear(&v);
            if (got) return true;
        } else {
            PropVariantClear(&v);
        }
    }
    return false;
}

static bool TryGetUInt(IWICMetadataQueryReader* qr,
                       std::initializer_list<const wchar_t*> paths, UINT& out) {
    for (auto p : paths) {
        PROPVARIANT v; PropVariantInit(&v);
        if (SUCCEEDED(qr->GetMetadataByName(p, &v))) {
            bool got = GetPropUInt(v, out);
            PropVariantClear(&v);
            if (got) return true;
        } else {
            PropVariantClear(&v);
        }
    }
    return false;
}

static ImageMetaInfo ReadImageMetadata(const std::wstring& path) {
    ImageMetaInfo info;

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        ULONGLONG size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        info.fileSize = FormatFileSize(size);
    }

    if (!InitWIC()) return info;

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(t_wicFactory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) || !decoder) {
        return info;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(decoder->GetFrame(0, &frame)) || !frame) {
        decoder->Release();
        return info;
    }

    UINT w = 0, h = 0;
    if (SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
        wchar_t buf[64];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%u × %u px", w, h);
        info.dimensions = buf;
    }

    IWICMetadataQueryReader* qr = nullptr;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&qr)) && qr) {
        std::wstring make, model, lens, lensMake;
        TryGetString(qr, { L"/app1/ifd/{ushort=271}", L"/ifd/{ushort=271}" }, make);
        TryGetString(qr, { L"/app1/ifd/{ushort=272}", L"/ifd/{ushort=272}" }, model);
        TryGetString(qr, { L"/app1/ifd/exif/{ushort=42036}", L"/ifd/exif/{ushort=42036}" }, lens);
        TryGetString(qr, { L"/app1/ifd/exif/{ushort=42035}", L"/ifd/exif/{ushort=42035}" }, lensMake);

        if (!make.empty() && !model.empty()) {
            if (model.compare(0, make.size(), make) == 0) info.camera = model;
            else info.camera = make + L" " + model;
        } else {
            info.camera = !model.empty() ? model : make;
        }
        info.lens = !lens.empty() ? lens : lensMake;

        double expTime = 0, fnum = 0, focal = 0;
        UINT iso = 0;
        bool hasExp = TryGetRational(qr, { L"/app1/ifd/exif/{ushort=33434}", L"/ifd/exif/{ushort=33434}" }, expTime);
        bool hasF   = TryGetRational(qr, { L"/app1/ifd/exif/{ushort=33437}", L"/ifd/exif/{ushort=33437}" }, fnum);
        bool hasFoc = TryGetRational(qr, { L"/app1/ifd/exif/{ushort=37386}", L"/ifd/exif/{ushort=37386}" }, focal);
        bool hasIso = TryGetUInt(qr, { L"/app1/ifd/exif/{ushort=34855}", L"/ifd/exif/{ushort=34855}" }, iso);

        std::wstring parts;
        wchar_t buf[128];
        if (hasExp && expTime > 0) {
            if (expTime < 1.0) _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"1/%.0f s", 1.0 / expTime);
            else _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.1f s", expTime);
            parts += buf;
        }
        if (hasF && fnum > 0) {
            if (!parts.empty()) parts += L", ";
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"f/%.1f", fnum);
            parts += buf;
        }
        if (hasIso && iso > 0) {
            if (!parts.empty()) parts += L", ";
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"ISO %u", iso);
            parts += buf;
        }
        if (hasFoc && focal > 0) {
            if (!parts.empty()) parts += L", ";
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f mm", focal);
            parts += buf;
        }
        info.exposure = parts;

        TryGetString(qr, { L"/app1/ifd/exif/{ushort=36867}", L"/ifd/exif/{ushort=36867}",
                            L"/app1/ifd/{ushort=306}", L"/ifd/{ushort=306}" }, info.dateTaken);

        std::wstring software, descr, comment;
        TryGetString(qr, { L"/app1/ifd/{ushort=305}", L"/ifd/{ushort=305}" }, software);
        TryGetString(qr, { L"/app1/ifd/{ushort=270}", L"/ifd/{ushort=270}" }, descr);
        TryGetString(qr, { L"/app1/ifd/exif/{ushort=37510}", L"/ifd/exif/{ushort=37510}" }, comment);

        bool looksLikeLabSim =
            software.find(L"LabSim")  != std::wstring::npos ||
            software.find(L"Lab_Sim") != std::wstring::npos ||
            software.find(L"Lab_sim") != std::wstring::npos ||
            software.find(L"LAB_SIM") != std::wstring::npos;

        std::wstring extra = !comment.empty() ? comment : descr;
        if (looksLikeLabSim && !extra.empty()) info.labsimInfo = extra;

        qr->Release();
    }

    frame->Release();
    decoder->Release();
    info.ok = true;
    return info;
}

// =============================================================================
// PREVIEW CANVAS
// =============================================================================
struct PreviewCanvasData {
    HBITMAP bmp;
};

static LRESULT CALLBACK PreviewCanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PreviewCanvasData* data = (PreviewCanvasData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            int W = rc.right, H = rc.bottom;

            HBRUSH bg = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            if (data && data->bmp && W > 0 && H > 0) {
                BITMAP bm = {};
                if (GetObject(data->bmp, sizeof(bm), &bm) &&
                    bm.bmWidth > 0 && bm.bmHeight > 0) {
                    double sx = (double)W / bm.bmWidth;
                    double sy = (double)H / bm.bmHeight;
                    double s = (sx < sy) ? sx : sy;
                    int dw = (int)(bm.bmWidth * s);
                    int dh = (int)(bm.bmHeight * s);
                    if (dw < 1) dw = 1;
                    if (dh < 1) dh = 1;
                    int dx = (W - dw) / 2;
                    int dy = (H - dh) / 2;

                    HDC hdcMem = CreateCompatibleDC(hdc);
                    HGDIOBJ old = SelectObject(hdcMem, data->bmp);
                    SetStretchBltMode(hdc, HALFTONE);
                    SetBrushOrgEx(hdc, 0, 0, nullptr);
                    StretchBlt(hdc, dx, dy, dw, dh, hdcMem, 0, 0,
                               bm.bmWidth, bm.bmHeight, SRCCOPY);
                    SelectObject(hdcMem, old);
                    DeleteDC(hdcMem);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY: {
            if (data) {
                if (data->bmp) DeleteObject(data->bmp);
                delete data;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterCanvasClass(HINSTANCE hi) {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = PreviewCanvasProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"LabViewPreviewCanvas";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    done = true;
}

static void SetCanvasBitmap(HWND hwnd, HBITMAP bmp) {
    PreviewCanvasData* data = (PreviewCanvasData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!data) {
        data = new PreviewCanvasData{nullptr};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);
    }
    if (data->bmp) DeleteObject(data->bmp);
    data->bmp = bmp;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// =============================================================================
// STAN APLIKACJI
// =============================================================================
struct PendingThumb {
    int itemIndex;
    std::wstring path;
};

struct ThumbResult {
    int itemIndex;
    std::wstring path;
    HBITMAP bmp;
    LONG generation;
};

struct PreviewResult {
    std::wstring path;
    HBITMAP bmp;
    LONG generation;
};

struct AppState {
    HINSTANCE hInst = nullptr;
    HWND hMain = nullptr;
    HWND hTree = nullptr;
    HWND hSplit = nullptr;
    HWND hRight = nullptr;
    HWND hSortLabel = nullptr;
    HWND hSortCombo = nullptr;
    HWND hFilterLabel = nullptr;
    HWND hFilterCombo = nullptr;
    HWND hRefreshBtn = nullptr;
    HWND hBrowseBtn = nullptr;
    HWND hStatusBar = nullptr;
    HWND hList = nullptr;
    HWND hMetaHeader = nullptr;
    HWND hMetaPanel = nullptr;

    HWND hPreview = nullptr;
    HWND hPreviewCanvas = nullptr;
    HWND hPreviewHint = nullptr;
    HWND hPreviewEditBtn = nullptr;
    int previewIdx = -1;

    Settings settings;
    std::wstring exeDir;
    std::wstring settingsPath;

    std::wstring currentFolder;
    std::vector<std::wstring> currentPaths;

    std::wstring cachedFolder;
    std::vector<std::wstring> cachedNames;

    HIMAGELIST hThumbList = nullptr;

    bool splitDragging = false;
    int  splitX = 320;
    int  splitDragOffset = 0;

    struct CacheEnt { HBITMAP bmp; HBITMAP big; FILETIME ft; bool hasBig; };
    std::map<std::wstring, CacheEnt> cache;
};

static AppState g;

static std::atomic<LONG> g_gen{0};
static std::atomic<LONG> g_previewGen{0};

// =============================================================================
// POPRAWKA #3: TRWAŁY WĄTEK ROBOCZY + KOLEJKA ZADAŃ
// Zamiast odłączać wątek per folder (co uniemożliwiało join przy zamykaniu),
// używamy jednego wątku roboczego z kolejką. Zamykanie sygnalizowane jest
// przez g_shuttingDown i wątek jest czekany (join) w WM_CLOSE.
// =============================================================================
struct ThumbJob {
    int itemIndex;
    std::wstring path;
    LONG gen;
};
struct PreviewJob {
    std::wstring path;
    LONG gen;
};

// Forward-decl: używane w WorkerThreadProc, zdefiniowane niżej.
static HBITMAP LoadBigPreviewBitmap(const std::wstring& path);

static std::mutex g_jobMutex;
static std::condition_variable g_jobCV;
static std::deque<ThumbJob> g_thumbJobs;
static std::deque<PreviewJob> g_previewJobs;
static std::thread g_workerThread;

static void WorkerThreadProc() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    while (!g_shuttingDown.load()) {
        ThumbJob tj;
        PreviewJob pj;
        bool hasT = false, hasP = false;

        {
            std::unique_lock<std::mutex> lk(g_jobMutex);
            g_jobCV.wait_for(lk, std::chrono::milliseconds(150), []{
                return g_shuttingDown.load()
                    || !g_previewJobs.empty()
                    || !g_thumbJobs.empty();
            });
            if (g_shuttingDown.load()) break;

            // Priorytet: duży podgląd (użytkownik czeka na wyświetlenie).
            if (!g_previewJobs.empty()) {
                pj = std::move(g_previewJobs.front());
                g_previewJobs.pop_front();
                hasP = true;
            } else if (!g_thumbJobs.empty()) {
                tj = std::move(g_thumbJobs.front());
                g_thumbJobs.pop_front();
                hasT = true;
            }
        }

        HWND hMain = g.hMain;
        if (!hMain || !IsWindow(hMain)) continue;

        if (hasP) {
            if (g_previewGen.load() != pj.gen) continue; // nieaktualne
            HBITMAP bmp = LoadBigPreviewBitmap(pj.path);
            if (!bmp) continue;
            if (g_shuttingDown.load() || g_previewGen.load() != pj.gen) {
                DeleteObject(bmp);
                continue;
            }
            PreviewResult* r = new PreviewResult();
            r->path = pj.path;
            r->bmp = bmp;
            r->generation = pj.gen;
            if (!PostMessageW(hMain, WM_APP_PREVIEW_READY, (WPARAM)r, 0)) {
                DeleteObject(r->bmp);
                delete r;
            }
        } else if (hasT) {
            if (g_gen.load() != tj.gen) continue;
            HBITMAP bmp = LoadImageScaled(tj.path, THUMB_W, THUMB_H);
            if (!bmp) {
                std::wstring ext = GetExt(tj.path);
                if (!ext.empty()) ext = ext.substr(1);
                for (auto& c : ext) c = std::towupper(c);
                bmp = MakeFallbackThumb(ext, THUMB_W, THUMB_H);
            }
            if (!bmp) continue;
            if (g_shuttingDown.load() || g_gen.load() != tj.gen) {
                DeleteObject(bmp);
                continue;
            }
            ThumbResult* r = new ThumbResult();
            r->itemIndex = tj.itemIndex;
            r->path = tj.path;
            r->bmp = bmp;
            r->generation = tj.gen;
            if (!PostMessageW(hMain, WM_APP_THUMB_READY, (WPARAM)r, 0)) {
                DeleteObject(r->bmp);
                delete r;
            }
        }
    }

    // POPRAWKA #4: zwolnienie WIC przed CoUninitialize
    ShutdownWIC();
    CoUninitialize();
}

static HBITMAP LoadBigPreviewBitmap(const std::wstring& path) {
    HBITMAP bmp = LoadImageScaled(path, PREVIEW_MAX_W, PREVIEW_MAX_H, /*pad=*/false);
    if (!bmp) {
        std::wstring ext = GetExt(path);
        if (!ext.empty()) ext = ext.substr(1);
        for (auto& c : ext) c = std::towupper(c);
        bmp = MakeFallbackThumb(ext, 400, 400);
    }
    return bmp;
}

// =============================================================================
// DRZEWO FOLDEROW
// POPRAWKA #1: alokowany std::wstring jest teraz zwalniany w TVN_DELETEITEM
// (patrz MainWndProc / WM_NOTIFY).
// =============================================================================
static void InsertTreeNode(HWND hTree, HTREEITEM parent,
                           const std::wstring& label, const std::wstring& path,
                           bool hasChildren) {
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    tvis.item.pszText = (LPWSTR)label.c_str();
    tvis.item.lParam = (LPARAM)new std::wstring(path);
    tvis.item.cChildren = hasChildren ? 1 : 0;
    HTREEITEM node = TreeView_InsertItem(hTree, &tvis);
    if (hasChildren && node) {
        TVINSERTSTRUCTW ph = {};
        ph.hParent = node;
        ph.hInsertAfter = TVI_LAST;
        ph.item.mask = TVIF_TEXT;
        ph.item.pszText = (LPWSTR)L"...";
        TreeView_InsertItem(hTree, &ph);
    }
}

static bool DirHasSubdirs(const std::wstring& path) {
    std::wstring pattern = path;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                found = true; break;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static void PopulateTreeRoots(HWND hTree) {
    TreeView_DeleteAllItems(hTree);

    wchar_t home[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);

    struct { const wchar_t* label; const wchar_t* sub; } fav[] = {
        { L"viewer.tree.desktop",   L"Desktop" },
        { L"viewer.tree.documents", L"Documents" },
        { L"viewer.tree.pictures",  L"Pictures" },
    };
    if (n > 0) {
        std::wstring hp = home;
        for (auto& f : fav) {
            std::wstring p = JoinPath(hp, f.sub);
            DWORD attr = GetFileAttributesW(p.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                InsertTreeNode(hTree, TVI_ROOT, Tr(f.label), p, DirHasSubdirs(p));
            }
        }
    }

    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1u << i)) {
            wchar_t letter = L'A' + i;
            std::wstring p;
            p += letter; p += L":\\";
            std::wstring lbl = p;
            InsertTreeNode(hTree, TVI_ROOT, lbl, p, true);
        }
    }
}

static void ExpandTreeNode(HWND hTree, HTREEITEM item) {
    TVITEMW tvi = {};
    tvi.mask = TVIF_PARAM;
    tvi.hItem = item;
    if (!TreeView_GetItem(hTree, &tvi)) return;
    std::wstring* pathPtr = (std::wstring*)tvi.lParam;
    if (!pathPtr) return;
    std::wstring path = *pathPtr;

    HTREEITEM child = TreeView_GetChild(hTree, item);
    if (!child) return;

    wchar_t buf[16] = {};
    TVITEMW ti = {}; ti.mask = TVIF_TEXT; ti.hItem = child;
    ti.pszText = buf; ti.cchTextMax = 16;
    TreeView_GetItem(hTree, &ti);
    if (wcscmp(buf, L"...") != 0) return;

    TreeView_DeleteItem(hTree, child);

    std::wstring pattern = path;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*";

    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(fd.cFileName, L".") != 0 &&
                wcscmp(fd.cFileName, L"..") != 0 &&
                fd.cFileName[0] != L'.') {
                dirs.push_back(fd.cFileName);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const std::wstring& a, const std::wstring& b) {
                  return _wcsicmp(a.c_str(), b.c_str()) < 0;
              });
    for (auto& d : dirs) {
        std::wstring full = JoinPath(path, d);
        InsertTreeNode(hTree, item, d, full, DirHasSubdirs(full));
    }
}

// =============================================================================
// FILTROWANIE / SORTOWANIE
// =============================================================================
static bool PassesFilter(const std::wstring& name, const std::wstring& mode) {
    if (mode == L"all") return true;
    if (mode == L"tiff") return IsLabsimTiff(name);
    if (mode == L"raw") return IsAnyRaw(name);
    if (mode == L"jpg") return IsJpg(name);
    return true;
}

static bool CompareName(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
}

static FILETIME GetFileTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    FILETIME ft = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) ft = fad.ftLastWriteTime;
    return ft;
}

static ULONGLONG GetFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return 0;
}

// =============================================================================
// LISTVIEW / CACHE
// =============================================================================
static void ClearList(HWND hList) {
    ListView_DeleteAllItems(hList);
    if (g.hThumbList) ImageList_RemoveAll(g.hThumbList);
    g.currentPaths.clear();
}

static void EnsureThumbList() {
    if (!g.hThumbList) {
        g.hThumbList = ImageList_Create(THUMB_W, THUMB_H, ILC_COLOR32, 32, 32);
    }
}

static HBITMAP LoadAndCacheThumb(const std::wstring& path) {
    auto it = g.cache.find(path);
    if (it != g.cache.end()) {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            if (CompareFileTime(&fad.ftLastWriteTime, &it->second.ft) == 0 && it->second.bmp) {
                return it->second.bmp;
            }
        }
        if (it->second.bmp) DeleteObject(it->second.bmp);
        if (it->second.hasBig && it->second.big) DeleteObject(it->second.big);
        g.cache.erase(it);
    }

    HBITMAP bmp = LoadImageScaled(path, THUMB_W, THUMB_H);
    if (!bmp) {
        std::wstring ext = GetExt(path);
        if (!ext.empty()) ext = ext.substr(1);
        for (auto& c : ext) c = std::towupper(c);
        bmp = MakeFallbackThumb(ext, THUMB_W, THUMB_H);
    }
    AppState::CacheEnt ent = {};
    ent.bmp = bmp;
    ent.hasBig = false;
    ent.big = nullptr;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) ent.ft = fad.ftLastWriteTime;
    g.cache[path] = ent;
    return bmp;
}

static HBITMAP GetCachedBigPreview(const std::wstring& path) {
    auto it = g.cache.find(path);
    if (it != g.cache.end() && it->second.hasBig && it->second.big) return it->second.big;
    return nullptr;
}

static void StoreBigPreviewInCache(const std::wstring& path, HBITMAP bigBmp) {
    auto it = g.cache.find(path);
    if (it != g.cache.end()) {
        if (it->second.hasBig && it->second.big && it->second.big != bigBmp)
            DeleteObject(it->second.big);
        it->second.big = bigBmp;
        it->second.hasBig = true;
    } else {
        HBITMAP smallBmp = LoadImageScaled(path, THUMB_W, THUMB_H);
        if (!smallBmp) {
            std::wstring ext = GetExt(path);
            if (!ext.empty()) ext = ext.substr(1);
            for (auto& c : ext) c = std::towupper(c);
            smallBmp = MakeFallbackThumb(ext, THUMB_W, THUMB_H);
        }
        AppState::CacheEnt ent = {};
        ent.bmp = smallBmp;
        ent.hasBig = true;
        ent.big = bigBmp;
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) ent.ft = fad.ftLastWriteTime;
        g.cache[path] = ent;
    }
}

// =============================================================================
// LISTOWANIE / ŁADOWANIE FOLDERU
// =============================================================================
static std::wstring ResolveRealPath(const std::wstring& path) {
    HANDLE hDir = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hDir == INVALID_HANDLE_VALUE) return L"";
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetFinalPathNameByHandleW(hDir, buf, MAX_PATH * 2, FILE_NAME_NORMALIZED);
    CloseHandle(hDir);
    if (n == 0 || n >= MAX_PATH * 2) return L"";
    std::wstring result = buf;
    if (result.size() >= 4 && result[0]==L'\\' && result[1]==L'\\' &&
        result[2]==L'?' && result[3]==L'\\') {
        result = result.substr(4);
        if (result.size() >= 4 && result.substr(0, 4) == L"UNC\\") {
            result = L"\\\\" + result.substr(4);
        }
    }
    return result;
}

static bool ListFolderNames(const std::wstring& folder, std::vector<std::wstring>& out) {
    out.clear();

    auto tryList = [&](const std::wstring& base) -> bool {
        std::wstring pattern = base;
        if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
        pattern += L"*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return false;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            out.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return true;
    };

    if (tryList(folder)) return true;

    std::wstring real = ResolveRealPath(folder);
    if (!real.empty() && real != folder) {
        if (tryList(real)) return true;
    }
    return false;
}

static void LoadThumbnails(HWND hList, const std::wstring& folder, bool useCache) {
    LONG newGen = ++g_gen;

    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ClearList(hList);

    std::vector<std::wstring> names;
    if (useCache && g.cachedFolder == folder && !g.cachedNames.empty()) {
        names = g.cachedNames;
    } else {
        if (!ListFolderNames(folder, names)) {
            if (g.hStatusBar) {
                SendMessageW(g.hStatusBar, SB_SETTEXTW, 0, (LPARAM)Tr(L"viewer.status.no_access").c_str());
                SendMessageW(g.hStatusBar, SB_SETTEXTW, 1, (LPARAM)L"");
            }
            SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(hList, nullptr, TRUE);
            return;
        }
        g.cachedFolder = folder;
        g.cachedNames = names;
    }

    std::vector<std::wstring> img;
    for (auto& n : names) {
        if (!IsAnyImage(n)) continue;
        if (!PassesFilter(n, g.settings.filterMode)) continue;
        img.push_back(n);
    }

    if (g.settings.sortMode == L"date") {
        std::sort(img.begin(), img.end(),
                  [&](const std::wstring& a, const std::wstring& b) {
                      FILETIME fa = GetFileTime(JoinPath(folder, a));
                      FILETIME fb = GetFileTime(JoinPath(folder, b));
                      return CompareFileTime(&fa, &fb) > 0;
                  });
    } else if (g.settings.sortMode == L"size") {
        std::sort(img.begin(), img.end(),
                  [&](const std::wstring& a, const std::wstring& b) {
                      return GetFileSize(JoinPath(folder, a)) >
                             GetFileSize(JoinPath(folder, b));
                  });
    } else {
        std::sort(img.begin(), img.end(), CompareName);
    }

    if (img.empty()) {
        std::wstring emptyMsg = (g.settings.filterMode != L"all")
            ? Tr(L"viewer.status.empty_filter")
            : Tr(L"viewer.status.empty");
        if (g.hStatusBar) {
            SendMessageW(g.hStatusBar, SB_SETTEXTW, 0, (LPARAM)emptyMsg.c_str());
            SendMessageW(g.hStatusBar, SB_SETTEXTW, 1, (LPARAM)folder.c_str());
        }
        SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hList, nullptr, TRUE);
        return;
    }

    int nTiff = 0, nRaw = 0, nJpg = 0;
    for (auto& n : img) {
        if (IsLabsimTiff(n)) ++nTiff;
        else if (IsAnyRaw(n)) ++nRaw;
        else if (IsJpg(n)) ++nJpg;
    }

    if (g.hStatusBar) {
        std::wstring left = TrF(L"viewer.status.counts", {
            { L"n",    std::to_wstring((int)img.size()) },
            { L"tiff", std::to_wstring(nTiff) },
            { L"raw",  std::to_wstring(nRaw) },
            { L"jpg",  std::to_wstring(nJpg) } });
        SendMessageW(g.hStatusBar, SB_SETTEXTW, 0, (LPARAM)left.c_str());
        SendMessageW(g.hStatusBar, SB_SETTEXTW, 1, (LPARAM)folder.c_str());
    }

    EnsureThumbList();
    ListView_SetImageList(hList, g.hThumbList, LVSIL_NORMAL);

    HBITMAP phBmp = MakeFallbackThumb(L"…", THUMB_W, THUMB_H);
    int phIdx = ImageList_Add(g.hThumbList, phBmp, nullptr);
    DeleteObject(phBmp);

    std::vector<PendingThumb> jobs;

    for (size_t i = 0; i < img.size(); ++i) {
        std::wstring full = JoinPath(folder, img[i]);

        int itemImageIdx = phIdx;
        auto it = g.cache.find(full);
        bool cachedValid = false;
        if (it != g.cache.end() && it->second.bmp) {
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &fad) &&
                CompareFileTime(&fad.ftLastWriteTime, &it->second.ft) == 0) {
                int idx = ImageList_Add(g.hThumbList, it->second.bmp, nullptr);
                if (idx >= 0) itemImageIdx = idx;
                cachedValid = true;
            }
        }
        if (!cachedValid) {
            PendingThumb pt;
            pt.itemIndex = (int)i;
            pt.path = full;
            jobs.push_back(pt);
        }

        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        lvi.iItem = (int)i;
        lvi.iImage = itemImageIdx;
        lvi.pszText = (LPWSTR)img[i].c_str();
        lvi.lParam = (LPARAM)i;
        ListView_InsertItem(hList, &lvi);

        g.currentPaths.push_back(full);
    }

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, nullptr, TRUE);

    if (!jobs.empty() && !g_shuttingDown.load()) {
        // POPRAWKA #3: zamiast startować nowy wątek (i detachować stary),
        // wrzucamy zadania do kolejki trwałego wątku roboczego.
        {
            std::lock_guard<std::mutex> lk(g_jobMutex);
            g_thumbJobs.clear(); // nowa partia unieważnia poprzednie
            for (auto& j : jobs) {
                ThumbJob tj;
                tj.itemIndex = j.itemIndex;
                tj.path = std::move(j.path);
                tj.gen = newGen;
                g_thumbJobs.push_back(std::move(tj));
            }
        }
        g_jobCV.notify_one();
    }
}

static void RefreshCurrentFolder() {
    if (g.currentFolder.empty()) return;
    for (auto& kv : g.cache) {
        if (kv.second.bmp)  DeleteObject(kv.second.bmp);
        if (kv.second.hasBig && kv.second.big) DeleteObject(kv.second.big);
    }
    g.cache.clear();
    g.cachedNames.clear();
    g.cachedFolder.clear();
    LoadThumbnails(g.hList, g.currentFolder, false);
}

// =============================================================================
// PANEL "INFORMACJE O PLIKU"
// =============================================================================
static void UpdateMetaPanel(const std::wstring& path) {
    if (!g.hMetaPanel) return;
    if (path.empty()) {
        SetWindowTextW(g.hMetaPanel, Tr(L"viewer.meta.none").c_str());
        return;
    }

    ImageMetaInfo info = ReadImageMetadata(path);

    std::wstring text;
    const wchar_t* name = PathFindFileNameW(path.c_str());
    text += name;
    text += L"\r\n";

    if (!info.dimensions.empty() || !info.fileSize.empty()) {
        text += info.dimensions;
        if (!info.dimensions.empty() && !info.fileSize.empty()) text += L"   •   ";
        text += info.fileSize;
        text += L"\r\n";
    }

    bool anyExif = false;
    if (!info.camera.empty())    { text += Tr(L"viewer.meta.camera")   + L": " + info.camera    + L"\r\n"; anyExif = true; }
    if (!info.lens.empty())      { text += Tr(L"viewer.meta.lens")     + L": " + info.lens      + L"\r\n"; anyExif = true; }
    if (!info.exposure.empty())  { text += Tr(L"viewer.meta.exposure") + L": " + info.exposure  + L"\r\n"; anyExif = true; }
    if (!info.dateTaken.empty()) { text += Tr(L"viewer.meta.date")     + L": " + info.dateTaken + L"\r\n"; anyExif = true; }
    if (!anyExif) text += Tr(L"viewer.meta.no_exif") + L"\r\n";

    if (!info.labsimInfo.empty()) {
        text += L"\r\nLabSim:\r\n";
        text += info.labsimInfo;
        text += L"\r\n";
    }

    SetWindowTextW(g.hMetaPanel, text.c_str());
}

// =============================================================================
// OKNO PODGLADU
// =============================================================================
static void PreviewShow(int idx);
static void ShowPreviewWindow();
static void ClosePreviewWindow();

static void LayoutPreviewWindow(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = 0;
    int toolbarH = 50;
    int hintH = 24;

    if (g.hPreviewCanvas)
        MoveWindow(g.hPreviewCanvas, pad, pad,
                   W - 2*pad, H - toolbarH - hintH - 2*pad, TRUE);

    if (g.hPreviewHint)
        MoveWindow(g.hPreviewHint, pad + 2, H - toolbarH - hintH, W - 2*pad, hintH, TRUE);

    int btnY = H - toolbarH + 10;
    HWND hPrev = GetDlgItem(hwnd, ID_PREVIEW_PREV);
    HWND hNext = GetDlgItem(hwnd, ID_PREVIEW_NEXT);
    HWND hEdit = GetDlgItem(hwnd, ID_PREVIEW_EDIT);
    HWND hClose = GetDlgItem(hwnd, ID_PREVIEW_CLOSE);
    if (hPrev)  MoveWindow(hPrev, 10, btnY, 130, 30, TRUE);
    if (hNext)  MoveWindow(hNext, 148, btnY, 130, 30, TRUE);
    if (hEdit)  MoveWindow(hEdit, 286, btnY, 160, 30, TRUE);
    if (hClose) MoveWindow(hClose, W - 148, btnY, 138, 30, TRUE);
}

static void UpdatePreviewTitle() {
    if (g.previewIdx < 0 || g.previewIdx >= (int)g.currentPaths.size()) return;
    const std::wstring& full = g.currentPaths[g.previewIdx];
    const wchar_t* name = PathFindFileNameW(full.c_str());
    wchar_t buf[512];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%s  (%d/%d)",
                 name, g.previewIdx + 1, (int)g.currentPaths.size());
    SetWindowTextW(g.hPreview, buf);
}

static void PreviewShow(int idx) {
    if (g.currentPaths.empty()) return;
    int n = (int)g.currentPaths.size();
    idx = ((idx % n) + n) % n;
    g.previewIdx = idx;

    ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(g.hList, idx, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g.hList, idx, FALSE);

    UpdatePreviewTitle();

    const std::wstring& full = g.currentPaths[idx];

    LONG previewGen = ++g_previewGen;

    HBITMAP cachedBig = GetCachedBigPreview(full);
    if (cachedBig) {
        HBITMAP copy = (HBITMAP)CopyImage(cachedBig, IMAGE_BITMAP, 0, 0, 0);
        SetCanvasBitmap(g.hPreviewCanvas, copy);
    } else {
        HBITMAP thumb = LoadAndCacheThumb(full);
        HBITMAP placeholder = thumb ? (HBITMAP)CopyImage(thumb, IMAGE_BITMAP, 0, 0, 0) : nullptr;
        SetCanvasBitmap(g.hPreviewCanvas, placeholder);

        // POPRAWKA #3: zamiast tworzyć nowy wątek — kolejkujemy do workera.
        if (!g_shuttingDown.load()) {
            {
                std::lock_guard<std::mutex> lk(g_jobMutex);
                g_previewJobs.clear(); // tylko najnowszy podgląd jest istotny
                PreviewJob pj;
                pj.path = full;
                pj.gen = previewGen;
                g_previewJobs.push_back(std::move(pj));
            }
            g_jobCV.notify_one();
        }
    }

    if (IsLabsimOpenable(full)) {
        ShowWindow(g.hPreviewEditBtn, SW_SHOW);
    } else {
        ShowWindow(g.hPreviewEditBtn, SW_HIDE);
    }

    std::wstring hint;
    if (IsAnyRaw(full)) {
        hint = Tr(L"viewer.preview.hint_raw");
    } else if (IsOtherRaw(full)) {
        hint = Tr(L"viewer.preview.hint_other_raw");
    } else {
        hint = L"";
    }
    SetWindowTextW(g.hPreviewHint, hint.c_str());

    UpdateMetaPanel(full);
}

static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            LayoutPreviewWindow(hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 500;
            mmi->ptMinTrackSize.y = 400;
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_PREVIEW_PREV)  { PreviewShow(g.previewIdx - 1); return 0; }
            if (id == ID_PREVIEW_NEXT)  { PreviewShow(g.previewIdx + 1); return 0; }
            if (id == ID_PREVIEW_CLOSE) { ClosePreviewWindow(); return 0; }
            if (id == ID_PREVIEW_EDIT)  {
                if (g.previewIdx >= 0 && g.previewIdx < (int)g.currentPaths.size()) {
                    extern void OpenInLabSim(const std::wstring&);
                    OpenInLabSim(g.currentPaths[g.previewIdx]);
                }
                return 0;
            }
            break;
        }
        case WM_KEYDOWN: {
            if (wp == VK_LEFT)  { PreviewShow(g.previewIdx - 1); return 0; }
            if (wp == VK_RIGHT || wp == VK_SPACE) { PreviewShow(g.previewIdx + 1); return 0; }
            if (wp == VK_ESCAPE) { ClosePreviewWindow(); return 0; }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wp;
            static HBRUSH s_darkBrush = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
            SetTextColor(hdcStatic, RGB(0xE0, 0xE0, 0xE0));
            SetBkColor(hdcStatic, RGB(0x20, 0x20, 0x20));
            return (LRESULT)s_darkBrush;
        }
        case WM_CLOSE:
            ClosePreviewWindow();
            return 0;
        case WM_DESTROY:
            g.hPreview = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterPreviewClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = PreviewWndProc;
    wc.hInstance = g.hInst;
    wc.lpszClassName = L"LabViewPreviewWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x20, 0x20, 0x20));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    done = true;
}

static void ShowPreviewWindow() {
    if (g.hPreview && IsWindow(g.hPreview)) {
        ShowWindow(g.hPreview, SW_SHOW);
        SetForegroundWindow(g.hPreview);
        return;
    }
    RegisterPreviewClass();
    RegisterCanvasClass(g.hInst);

    g.hPreview = CreateWindowExW(
        0, L"LabViewPreviewWnd", Tr(L"viewer.preview.title").c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 780,
        g.hMain, nullptr, g.hInst, nullptr);

    g.hPreviewCanvas = CreateWindowExW(
        0, L"LabViewPreviewCanvas", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100,
        g.hPreview, (HMENU)(INT_PTR)ID_PREVIEW_CANVAS, g.hInst, nullptr);

    g.hPreviewHint = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 100, 20,
        g.hPreview, nullptr, g.hInst, nullptr);
    ApplyFontToWindow(g.hPreviewHint);

    HWND hPrev = CreateWindowExW(0, L"BUTTON", Tr(L"viewer.preview.prev").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 130, 30,
        g.hPreview, (HMENU)(INT_PTR)ID_PREVIEW_PREV, g.hInst, nullptr);
    ApplyFontToWindow(hPrev);

    HWND hNext = CreateWindowExW(0, L"BUTTON", Tr(L"viewer.preview.next").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 130, 30,
        g.hPreview, (HMENU)(INT_PTR)ID_PREVIEW_NEXT, g.hInst, nullptr);
    ApplyFontToWindow(hNext);

    g.hPreviewEditBtn = CreateWindowExW(0, L"BUTTON", Tr(L"viewer.preview.edit").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 160, 30,
        g.hPreview, (HMENU)(INT_PTR)ID_PREVIEW_EDIT, g.hInst, nullptr);
    ApplyFontToWindow(g.hPreviewEditBtn);

    HWND hClose = CreateWindowExW(0, L"BUTTON", Tr(L"viewer.preview.close").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 138, 30,
        g.hPreview, (HMENU)(INT_PTR)ID_PREVIEW_CLOSE, g.hInst, nullptr);
    ApplyFontToWindow(hClose);

    if (g.previewIdx >= 0 && g.previewIdx < (int)g.currentPaths.size()) {
        if (IsLabsimOpenable(g.currentPaths[g.previewIdx]))
            ShowWindow(g.hPreviewEditBtn, SW_SHOW);
        else
            ShowWindow(g.hPreviewEditBtn, SW_HIDE);
    }

    ApplyDarkMode(g.hPreview, true);

    LayoutPreviewWindow(g.hPreview);

    ShowWindow(g.hPreview, SW_SHOW);
    UpdateWindow(g.hPreview);
    SetForegroundWindow(g.hPreview);
}

static void ClosePreviewWindow() {
    if (g.hPreview && IsWindow(g.hPreview)) DestroyWindow(g.hPreview);
    g.hPreview = nullptr;
    g.hPreviewCanvas = nullptr;
    g.previewIdx = -1;
}

// =============================================================================
// LAB_SIM.EXE
// =============================================================================
void OpenInLabSim(const std::wstring& path) {
    std::wstring lab = JoinPath(g.exeDir, L"Lab_Sim.exe");
    DWORD attr = GetFileAttributesW(lab.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::wstring msg = TrF(L"viewer.labsim.not_found", { { L"path", lab } });
        MessageBoxW(g.hMain, msg.c_str(), Tr(L"viewer.labsim.not_found_title").c_str(),
                    MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring cmd = L"\"" + lab + L"\" \"" + path + L"\"";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        MessageBoxW(g.hMain, Tr(L"viewer.labsim.launch_failed").c_str(),
                    Tr(L"error.title").c_str(), MB_OK | MB_ICONERROR);
    }
}

// =============================================================================
// DWUKLIK
// =============================================================================
static void OnListDoubleClick(int idx) {
    if (idx < 0 || idx >= (int)g.currentPaths.size()) return;
    const std::wstring& path = g.currentPaths[idx];
    if (IsLabsimOpenable(path)) {
        OpenInLabSim(path);
    } else if (IsOtherRaw(path)) {
        MessageBoxW(g.hMain,
            Tr(L"viewer.labsim.unsupported").c_str(),
            Tr(L"viewer.labsim.unsupported_title").c_str(),
            MB_OK | MB_ICONINFORMATION);
    } else if (IsJpg(path)) {
        ShowPreviewWindow();
        PreviewShow(idx);
    }
}

static LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_SPACE) {
            int sel = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (sel >= 0) { ShowPreviewWindow(); PreviewShow(sel); }
            return 0;
        }
        if (wp == VK_RETURN) {
            int sel = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (sel >= 0) OnListDoubleClick(sel);
            return 0;
        }
        if (wp == VK_F5) { RefreshCurrentFolder(); return 0; }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// =============================================================================
// WYBOR FOLDERU
// =============================================================================
static void BrowseForFolder() {
    BROWSEINFOW bi = {};
    bi.hwndOwner = g.hMain;
    std::wstring browseTitle = Tr(L"viewer.browse.title");
    bi.lpszTitle = browseTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH] = {};
    if (SHGetPathFromIDListW(pidl, path)) {
        g.currentFolder = path;
        g.settings.lastFolder = path;
        LoadThumbnails(g.hList, g.currentFolder, false);
        UpdateMetaPanel(L"");
    }
    CoTaskMemFree(pidl);
}

// =============================================================================
// OKNO "SKRÓTY KLAWISZOWE"
// =============================================================================
static LRESULT CALLBACK ShortcutsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND s_hList = nullptr;
    static HFONT s_hMono = nullptr;
    static HFONT s_hHead = nullptr;

    switch (msg) {
        case WM_CREATE: {
            RECT rc; GetClientRect(hwnd, &rc);

            LOGFONTW lf = {};
            lf.lfHeight = -14;
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, L"Consolas");
            s_hMono = CreateFontIndirectW(&lf);

            LOGFONTW lh = {};
            lh.lfHeight = -16;
            lh.lfWeight = FW_BOLD;
            lh.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lh.lfFaceName, L"Segoe UI");
            s_hHead = CreateFontIndirectW(&lh);

            s_hList = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                10, 45, rc.right - 20, rc.bottom - 95,
                hwnd, (HMENU)(INT_PTR)9200, nullptr, nullptr);
            if (s_hMono) SendMessageW(s_hList, WM_SETFONT, (WPARAM)s_hMono, TRUE);

            for (auto& sc : SHORTCUTS) {
                std::wstring line = L"  ";
                line += sc.keys;
                while (line.size() < 14) line += L' ';
                line += L"→   ";
                line += Tr(sc.trKey);
                SendMessageW(s_hList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
            }

            CreateWindowExW(0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                rc.right - 110, rc.bottom - 42, 95, 30,
                hwnd, (HMENU)IDOK, nullptr, nullptr);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
            HGDIOBJ old = SelectObject(hdc, s_hHead);
            { std::wstring head = Tr(L"viewer.shortcuts.title"); TextOutW(hdc, 15, 15, head.c_str(), (int)head.size()); }
            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (s_hMono) { DeleteObject(s_hMono); s_hMono = nullptr; }
            if (s_hHead) { DeleteObject(s_hHead); s_hHead = nullptr; }
            s_hList = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Forward helper (używany także z MainWndProc)
static void ShowShortcutsStandalone(HWND hParent);

// =============================================================================
// OKNO "O PROGRAMIE"
// =============================================================================
static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            RECT rc; GetClientRect(hwnd, &rc);
            CreateWindowExW(0, L"BUTTON", Tr(L"viewer.about.shortcuts_btn").c_str(),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                20, rc.bottom - 50, 220, 32,
                hwnd, (HMENU)(INT_PTR)IDM_SHORTCUTS, nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                rc.right - 110, rc.bottom - 50, 90, 32,
                hwnd, (HMENU)IDOK, nullptr, nullptr);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);

            // Tło
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            SetBkMode(hdc, TRANSPARENT);

            // Ikona aplikacji
            HICON hIcon = LoadIconW(g.hInst, MAKEINTRESOURCEW(IDI_APPICON));
            if (!hIcon) hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            DrawIconEx(hdc, 20, 20, hIcon, 48, 48, 0, nullptr, DI_NORMAL);

            // Tytuł "Lab View"
            HFONT hTitle = CreateFontW(-28, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                       DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            HGDIOBJ old = SelectObject(hdc, hTitle);
            SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
            TextOutW(hdc, 80, 20, L"Lab View", 8);

            // Podtytuł "Przeglądarka zdjęć v2.5"
            HFONT hSub = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                     DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            SelectObject(hdc, hSub);
            SetTextColor(hdc, RGB(0x60, 0x60, 0x60));
            std::wstring sub = Tr(L"viewer.about.subtitle") + L"  v";
            sub += APP_VERSION;
            TextOutW(hdc, 82, 55, sub.c_str(), (int)sub.size());

            // Separator
            HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xE0, 0xE0, 0xE0));
            HGDIOBJ oldPen = SelectObject(hdc, sep);
            MoveToEx(hdc, 20, 95, nullptr);
            LineTo(hdc, rc.right - 20, 95);
            SelectObject(hdc, oldPen);
            DeleteObject(sep);

            // Body info
            HFONT hInfo = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                      DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
            SelectObject(hdc, hInfo);

            struct AboutRow { const wchar_t* key; std::wstring value; };
            AboutRow rows[] = {
                { L"viewer.about.author",  APP_AUTHOR },
                { L"viewer.about.version", APP_VERSION },
                { L"viewer.about.build",   APP_BUILD },
                { L"viewer.about.date",    APP_DATE },
                { L"viewer.about.license", L"Apache 2.0" },
            };
            SetTextColor(hdc, RGB(0x30, 0x30, 0x30));
            int rowY = 115;
            for (auto& r : rows) {
                std::wstring label = Tr(r.key) + L":";
                TextOutW(hdc, 20, rowY, label.c_str(), (int)label.size());
                TextOutW(hdc, 170, rowY, r.value.c_str(), (int)r.value.size());
                rowY += 24;
            }
            const wchar_t* licUrl = L"http://www.apache.org/licenses/LICENSE-2.0";
            TextOutW(hdc, 170, rowY, licUrl, (int)wcslen(licUrl));

            // Stopka
            RECT rcFoot = { 20, rc.bottom - 78, rc.right - 20, rc.bottom - 56 };
            SetTextColor(hdc, RGB(0x80, 0x80, 0x80));
            { std::wstring foot = Tr(L"viewer.about.companion"); DrawTextW(hdc, foot.c_str(), -1, &rcFoot, DT_LEFT | DT_SINGLELINE); }

            SelectObject(hdc, old);
            DeleteObject(hTitle);
            DeleteObject(hSub);
            DeleteObject(hInfo);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDM_SHORTCUTS) {
                ShowShortcutsStandalone(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterAboutClasses() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wcAbout = {};
    wcAbout.lpfnWndProc = AboutWndProc;
    wcAbout.hInstance = g.hInst;
    wcAbout.lpszClassName = L"LabViewAbout";
    wcAbout.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcAbout.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcAbout.hIcon = LoadIconW(g.hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassW(&wcAbout);

    WNDCLASSW wcShort = {};
    wcShort.lpfnWndProc = ShortcutsWndProc;
    wcShort.hInstance = g.hInst;
    wcShort.lpszClassName = L"LabViewShortcuts";
    wcShort.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcShort.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcShort.hIcon = LoadIconW(g.hInst, MAKEINTRESOURCEW(IDI_APPICON));
    RegisterClassW(&wcShort);

    registered = true;
}

static void ShowAboutWindow(HWND hParent) {
    RegisterAboutClasses();

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"LabViewAbout",
        Tr(L"about.title").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 440,
        hParent, nullptr, g.hInst, nullptr);
    if (!hwnd) return;

    RECT rcP, rcW;
    GetWindowRect(hParent, &rcP);
    GetWindowRect(hwnd, &rcW);
    int x = rcP.left + ((rcP.right - rcP.left) - (rcW.right - rcW.left)) / 2;
    int y = rcP.top + ((rcP.bottom - rcP.top) - (rcW.bottom - rcW.top)) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ApplyDarkMode(hwnd, IsSystemDarkMode());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

static void ShowShortcutsStandalone(HWND hParent) {
    RegisterAboutClasses();

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"LabViewShortcuts",
        Tr(L"viewer.shortcuts.title").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 640,
        hParent, nullptr, g.hInst, nullptr);
    if (!hwnd) return;

    RECT rcP, rcW;
    GetWindowRect(hParent, &rcP);
    GetWindowRect(hwnd, &rcW);
    int x = rcP.left + ((rcP.right - rcP.left) - (rcW.right - rcW.left)) / 2;
    int y = rcP.top + ((rcP.bottom - rcP.top) - (rcW.bottom - rcW.top)) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ApplyDarkMode(hwnd, IsSystemDarkMode());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

// =============================================================================
// SPLITTER
// =============================================================================
static void LayoutMainWindow(HWND hMain);

static LRESULT CALLBACK SplitterWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            g.splitDragging = true;
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(g.hMain, &pt);
            g.splitDragOffset = pt.x - g.splitX;
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g.splitDragging) {
                POINT pt; GetCursorPos(&pt);
                ScreenToClient(g.hMain, &pt);
                int newX = pt.x - g.splitDragOffset;
                RECT rc; GetClientRect(g.hMain, &rc);
                if (newX < 180) newX = 180;
                if (newX > rc.right - 300) newX = rc.right - 300;
                g.splitX = newX;
                g.settings.sashPos = newX;
                LayoutMainWindow(g.hMain);
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.splitDragging) {
                g.splitDragging = false;
                ReleaseCapture();
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============================================================================
// KONTENER
// =============================================================================
static LRESULT CALLBACK ContainerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
        case WM_NOTIFY:
            return SendMessageW(GetParent(hwnd), msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============================================================================
// POPRAWKA #2: LAYOUT — POPRAWNA LOGIKA WYSOKOŚCI DRZEWA
// =============================================================================
static void LayoutMainWindow(HWND hMain) {
    RECT rc; GetClientRect(hMain, &rc);
    int W = rc.right, H = rc.bottom;

    int statusH = STATUS_H;
    if (g.hStatusBar && IsWindow(g.hStatusBar)) {
        RECT sr; GetWindowRect(g.hStatusBar, &sr);
        statusH = sr.bottom - sr.top;
    }
    int contentH = H - statusH;

    if (g.splitX < 180) g.splitX = 180;
    if (g.splitX > W - 300) g.splitX = W - 300;

    int leftW = g.splitX - 4;
    int metaTotalH = META_HEADER_H + META_PANEL_H;
    int treeH = contentH - metaTotalH;

    // POPRAWKA #2: wymuś minimum 100 px na drzewo, zostaw min. 50 px na meta.
    if (treeH < 100) treeH = 100;
    if (treeH > contentH - 50) treeH = contentH - 50;
    if (treeH < 40) treeH = 40;

    MoveWindow(g.hTree, 0, 0, leftW, treeH, TRUE);
    if (g.hMetaHeader)
        MoveWindow(g.hMetaHeader, 8, treeH + 6, leftW - 16, META_HEADER_H, TRUE);
    if (g.hMetaPanel)
        MoveWindow(g.hMetaPanel, 6, treeH + 6 + META_HEADER_H,
                   leftW - 12, contentH - treeH - META_HEADER_H - 10, TRUE);

    MoveWindow(g.hSplit, g.splitX - 4, 0, 4, contentH, TRUE);
    MoveWindow(g.hRight, g.splitX, 0, W - g.splitX, contentH, TRUE);

    RECT rr; GetClientRect(g.hRight, &rr);
    int rW = rr.right;
    int rH = rr.bottom;

    int y = MARGIN;

    if (g.hSortLabel) MoveWindow(g.hSortLabel, MARGIN, y + 4, 50, 22, TRUE);
    if (g.hSortCombo) MoveWindow(g.hSortCombo, MARGIN + 55, y, 170, 26, TRUE);

    if (g.hFilterLabel) MoveWindow(g.hFilterLabel, MARGIN + 240, y + 4, 55, 22, TRUE);
    if (g.hFilterCombo) MoveWindow(g.hFilterCombo, MARGIN + 295, y, 130, 26, TRUE);

    if (g.hBrowseBtn) {
        int btnW = 180;
        MoveWindow(g.hBrowseBtn, rW - MARGIN - btnW, y, btnW, 26, TRUE);
        if (g.hRefreshBtn) {
            int btnW2 = 130;
            MoveWindow(g.hRefreshBtn, rW - MARGIN - btnW - 8 - btnW2, y, btnW2, 26, TRUE);
        }
    }

    int listY = TOOLBAR_H;
    MoveWindow(g.hList, 0, listY, rW, rH - listY, TRUE);
}

// =============================================================================
// AKCJE MENU / SKRÓTÓW (wspólne dla WM_COMMAND i akceleratorów)
// =============================================================================
static void ApplySortMode(const std::wstring& mode) {
    g.settings.sortMode = mode;
    int sel = 0;
    if (mode == L"date") sel = 1;
    else if (mode == L"size") sel = 2;
    if (g.hSortCombo) SendMessageW(g.hSortCombo, CB_SETCURSEL, sel, 0);
    if (!g.currentFolder.empty())
        LoadThumbnails(g.hList, g.currentFolder, true);
}

static void ApplyFilterMode(const std::wstring& mode) {
    g.settings.filterMode = mode;
    int sel = 0;
    if (mode == L"tiff") sel = 1;
    else if (mode == L"raw") sel = 2;
    else if (mode == L"jpg") sel = 3;
    if (g.hFilterCombo) SendMessageW(g.hFilterCombo, CB_SETCURSEL, sel, 0);
    if (!g.currentFolder.empty())
        LoadThumbnails(g.hList, g.currentFolder, true);
}

// =============================================================================
// JĘZYK: MENU, KONTROLKI, PRZEŁĄCZANIE
// =============================================================================
static void FillSortCombo(HWND h, int sel) {
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"viewer.sort.name").c_str());
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"viewer.sort.date").c_str());
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"viewer.sort.size").c_str());
    SendMessageW(h, CB_SETCURSEL, sel, 0);
}

static void FillFilterCombo(HWND h, int sel) {
    SendMessageW(h, CB_RESETCONTENT, 0, 0);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)Tr(L"viewer.filter.all").c_str());
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"TIFF");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"RAW");
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"JPG");
    SendMessageW(h, CB_SETCURSEL, sel, 0);
}

static HMENU BuildMainMenu() {
    HMENU bar = CreateMenu();
    auto add = [&](HMENU m, int id, const wchar_t* key, const wchar_t* accel) {
        std::wstring t = Tr(key);
        if (accel && *accel) { t += L"\t"; t += accel; }
        AppendMenuW(m, MF_STRING, (UINT_PTR)id, t.c_str());
    };
    auto popup = [&](HMENU sub, const wchar_t* key) {
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)sub, Tr(key).c_str());
    };

    HMENU hFile = CreatePopupMenu();
    add(hFile, ID_BROWSE_BTN,  L"viewer.menu.file.open",    L"Ctrl+O");
    add(hFile, ID_REFRESH_BTN, L"viewer.menu.file.refresh", L"F5");
    AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
    add(hFile, IDM_EXIT,       L"viewer.menu.file.exit",    L"Alt+F4");
    popup(hFile, L"viewer.menu.file");

    HMENU hSort = CreatePopupMenu();
    add(hSort, IDM_SORT_NAME, L"viewer.menu.sort.name", L"Ctrl+S");
    add(hSort, IDM_SORT_DATE, L"viewer.menu.sort.date", L"Ctrl+D");
    add(hSort, IDM_SORT_SIZE, L"viewer.menu.sort.size", L"Ctrl+Z");
    popup(hSort, L"viewer.menu.sort");

    HMENU hFilter = CreatePopupMenu();
    add(hFilter, IDM_FILTER_ALL, L"viewer.menu.filter.all", L"Ctrl+A");
    AppendMenuW(hFilter, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hFilter, MF_STRING, IDM_FILTER_RAW,  L"&RAW\tCtrl+1");
    AppendMenuW(hFilter, MF_STRING, IDM_FILTER_JPG,  L"&JPG\tCtrl+2");
    AppendMenuW(hFilter, MF_STRING, IDM_FILTER_TIFF, L"&TIFF\tCtrl+3");
    popup(hFilter, L"viewer.menu.filter");

    if (!g_langs.empty()) {
        HMENU hLang = CreatePopupMenu();
        for (size_t i = 0; i < g_langs.size(); ++i) {
            UINT fl = MF_STRING;
            if (_wcsicmp(g_langs[i].code.c_str(), g_langCode.c_str()) == 0) fl |= MF_CHECKED;
            AppendMenuW(hLang, fl, (UINT_PTR)(IDM_LANG_BASE + (int)i), g_langs[i].name.c_str());
        }
        popup(hLang, L"viewer.menu.language");
    }

    HMENU hHelp = CreatePopupMenu();
    add(hHelp, IDM_ABOUT,     L"viewer.menu.help.about",     L"F1");
    add(hHelp, IDM_SHORTCUTS, L"viewer.menu.help.shortcuts", L"F2");
    popup(hHelp, L"viewer.menu.help");

    return bar;
}

// Odświeża wszystkie teksty w oknie głównym (i w podglądzie, jeśli otwarty).
static void ApplyLanguageToUi() {
    if (!g.hMain) return;

    HMENU oldMenu = GetMenu(g.hMain);
    SetMenu(g.hMain, BuildMainMenu());
    if (oldMenu) DestroyMenu(oldMenu);
    DrawMenuBar(g.hMain);

    if (g.hMetaHeader)  SetWindowTextW(g.hMetaHeader,  Tr(L"viewer.meta.header").c_str());
    if (g.hSortLabel)   SetWindowTextW(g.hSortLabel,   Tr(L"viewer.toolbar.sort").c_str());
    if (g.hFilterLabel) SetWindowTextW(g.hFilterLabel, Tr(L"viewer.toolbar.filter").c_str());
    if (g.hRefreshBtn)  SetWindowTextW(g.hRefreshBtn,  Tr(L"viewer.toolbar.refresh").c_str());
    if (g.hBrowseBtn)   SetWindowTextW(g.hBrowseBtn,   Tr(L"viewer.toolbar.browse").c_str());

    if (g.hSortCombo) {
        int sel = (int)SendMessageW(g.hSortCombo, CB_GETCURSEL, 0, 0);
        FillSortCombo(g.hSortCombo, sel < 0 ? 0 : sel);
    }
    if (g.hFilterCombo) {
        int sel = (int)SendMessageW(g.hFilterCombo, CB_GETCURSEL, 0, 0);
        FillFilterCombo(g.hFilterCombo, sel < 0 ? 0 : sel);
    }

    // Etykiety korzeni "Pulpit / Dokumenty / Obrazy" w drzewie (bez przebudowy drzewa).
    if (g.hTree) {
        wchar_t home[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            struct { const wchar_t* key; const wchar_t* sub; } fav[] = {
                { L"viewer.tree.desktop",   L"Desktop" },
                { L"viewer.tree.documents", L"Documents" },
                { L"viewer.tree.pictures",  L"Pictures" },
            };
            for (HTREEITEM it = TreeView_GetRoot(g.hTree); it; it = TreeView_GetNextSibling(g.hTree, it)) {
                TVITEMW tv = {};
                tv.mask = TVIF_PARAM;
                tv.hItem = it;
                if (!TreeView_GetItem(g.hTree, &tv) || !tv.lParam) continue;
                const std::wstring& pth = *(std::wstring*)tv.lParam;
                for (auto& f : fav) {
                    if (_wcsicmp(pth.c_str(), JoinPath(home, f.sub).c_str()) == 0) {
                        std::wstring lbl = Tr(f.key);
                        TVITEMW sv = {};
                        sv.mask = TVIF_TEXT;
                        sv.hItem = it;
                        sv.pszText = (LPWSTR)lbl.c_str();
                        TreeView_SetItem(g.hTree, &sv);
                    }
                }
            }
        }
    }

    // Panel informacji o pliku + pasek statusu (odświeżenie listy odtwarza napisy).
    int selItem = g.hList ? ListView_GetNextItem(g.hList, -1, LVNI_SELECTED) : -1;
    if (!g.currentFolder.empty()) {
        LoadThumbnails(g.hList, g.currentFolder, true);
        UpdateMetaPanel(L"");
    } else {
        UpdateMetaPanel(L"");
        if (g.hStatusBar) {
            SendMessageW(g.hStatusBar, SB_SETTEXTW, 0, (LPARAM)L"");
            SendMessageW(g.hStatusBar, SB_SETTEXTW, 1, (LPARAM)L"");
        }
    }
    (void)selItem;

    // Okno podglądu
    if (g.hPreview && IsWindow(g.hPreview)) {
        SetWindowTextW(g.hPreview, Tr(L"viewer.preview.title").c_str());
        SetDlgItemTextW(g.hPreview, ID_PREVIEW_PREV,  Tr(L"viewer.preview.prev").c_str());
        SetDlgItemTextW(g.hPreview, ID_PREVIEW_NEXT,  Tr(L"viewer.preview.next").c_str());
        SetDlgItemTextW(g.hPreview, ID_PREVIEW_EDIT,  Tr(L"viewer.preview.edit").c_str());
        SetDlgItemTextW(g.hPreview, ID_PREVIEW_CLOSE, Tr(L"viewer.preview.close").c_str());
    }
}

static void SwitchLanguage(const std::wstring& code) {
    if (_wcsicmp(code.c_str(), g_langCode.c_str()) == 0) return;
    SetLanguageCode(code);
    g.settings.language = g_langCode;
    ApplyLanguageToUi();
}

// =============================================================================
// GLOWNE OKNO
// =============================================================================
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.hMain = hwnd;
            HINSTANCE hi = g.hInst;
            InitCommonControls();

            WNDCLASSW wcSplit = {};
            wcSplit.lpfnWndProc = SplitterWndProc;
            wcSplit.hInstance = hi;
            wcSplit.lpszClassName = L"LabViewSplitter";
            wcSplit.hCursor = LoadCursor(nullptr, IDC_SIZEWE);
            wcSplit.hbrBackground = (HBRUSH)(COLOR_BTNSHADOW + 1);
            RegisterClassW(&wcSplit);

            WNDCLASSW wcCont = {};
            wcCont.lpfnWndProc = ContainerWndProc;
            wcCont.hInstance = hi;
            wcCont.lpszClassName = L"LabViewContainer";
            wcCont.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wcCont.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
            RegisterClassW(&wcCont);

            g.hTree = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES |
                TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
                TVS_FULLROWSELECT | TVS_TRACKSELECT,
                0, 0, 300, 600, hwnd, (HMENU)(INT_PTR)ID_TREE, hi, nullptr);
            SetWindowTheme(g.hTree, L"Explorer", nullptr);
            TreeView_SetExtendedStyle(g.hTree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
            TreeView_SetItemHeight(g.hTree, 24);
            ApplyFontToWindow(g.hTree);
            PopulateTreeRoots(g.hTree);

            g.hMetaHeader = CreateWindowExW(
                0, L"STATIC", Tr(L"viewer.meta.header").c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                8, 0, 260, META_HEADER_H, hwnd, (HMENU)(INT_PTR)ID_META_HEADER, hi, nullptr);
            ApplyFontToWindow(g.hMetaHeader);

            g.hMetaPanel = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", Tr(L"viewer.meta.none").c_str(),
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_LEFT,
                6, 0, 260, META_PANEL_H, hwnd, (HMENU)(INT_PTR)ID_META_PANEL, hi, nullptr);
            ApplyFontToWindow(g.hMetaPanel);

            g.hSplit = CreateWindowExW(
                0, L"LabViewSplitter", L"",
                WS_CHILD | WS_VISIBLE,
                296, 0, 4, 600, hwnd, (HMENU)(INT_PTR)ID_SPLIT, hi, nullptr);

            g.hRight = CreateWindowExW(
                0, L"LabViewContainer", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                300, 0, 500, 600, hwnd, nullptr, hi, nullptr);

            g.hSortLabel = CreateWindowExW(
                0, L"STATIC", Tr(L"viewer.toolbar.sort").c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                MARGIN, 14, 50, 22, g.hRight, nullptr, hi, nullptr);
            ApplyFontToWindow(g.hSortLabel);

            g.hSortCombo = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                MARGIN + 55, MARGIN, 170, 200, g.hRight,
                (HMENU)(INT_PTR)ID_SORT_COMBO, hi, nullptr);
            int sortSel = 0;
            if (g.settings.sortMode == L"date") sortSel = 1;
            else if (g.settings.sortMode == L"size") sortSel = 2;
            FillSortCombo(g.hSortCombo, sortSel);
            ApplyFontToWindow(g.hSortCombo);

            g.hFilterLabel = CreateWindowExW(
                0, L"STATIC", Tr(L"viewer.toolbar.filter").c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                MARGIN + 240, 14, 55, 22, g.hRight, nullptr, hi, nullptr);
            ApplyFontToWindow(g.hFilterLabel);

            g.hFilterCombo = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                MARGIN + 295, MARGIN, 130, 200, g.hRight,
                (HMENU)(INT_PTR)ID_FILTER_COMBO, hi, nullptr);
            int filtSel = 0;
            if (g.settings.filterMode == L"tiff") filtSel = 1;
            else if (g.settings.filterMode == L"raw") filtSel = 2;
            else if (g.settings.filterMode == L"jpg") filtSel = 3;
            FillFilterCombo(g.hFilterCombo, filtSel);
            ApplyFontToWindow(g.hFilterCombo);

            g.hRefreshBtn = CreateWindowExW(
                0, L"BUTTON", Tr(L"viewer.toolbar.refresh").c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, MARGIN, 130, 26, g.hRight,
                (HMENU)(INT_PTR)ID_REFRESH_BTN, hi, nullptr);
            ApplyFontToWindow(g.hRefreshBtn);

            g.hBrowseBtn = CreateWindowExW(
                0, L"BUTTON", Tr(L"viewer.toolbar.browse").c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, MARGIN, 180, 26, g.hRight,
                (HMENU)(INT_PTR)ID_BROWSE_BTN, hi, nullptr);
            ApplyFontToWindow(g.hBrowseBtn);

            g.hStatusBar = CreateWindowExW(
                0, STATUSCLASSNAMEW, L"",
                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STATUSBAR, hi, nullptr);
            ApplyFontToWindow(g.hStatusBar);

            int parts[2] = { 400, -1 };
            SendMessageW(g.hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

            g.hList = CreateWindowExW(
                WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL |
                LVS_SHOWSELALWAYS | LVS_AUTOARRANGE | WS_VSCROLL | WS_HSCROLL,
                0, 60, 500, 500, g.hRight,
                (HMENU)(INT_PTR)ID_LIST, hi, nullptr);
            SetWindowTheme(g.hList, L"Explorer", nullptr);
            DWORD exStyle = LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                            LVS_EX_INFOTIP | LVS_EX_BORDERSELECT;
            ListView_SetExtendedListViewStyle(g.hList, exStyle);
            ListView_SetIconSpacing(g.hList, ICON_SPACING_X, ICON_SPACING_Y);
            ApplyFontToWindow(g.hList);

            SetWindowSubclass(g.hList, ListSubclassProc, 1, 0);

            // Menu główne (tworzone dynamicznie, teksty z languages\*.json)
            SetMenu(hwnd, BuildMainMenu());

            g.splitX = g.settings.sashPos;
            LayoutMainWindow(hwnd);
            return 0;
        }

        case WM_SIZE: {
            if (g.hStatusBar) SendMessageW(g.hStatusBar, WM_SIZE, 0, 0);
            LayoutMainWindow(hwnd);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 800;
            mmi->ptMinTrackSize.y = 500;
            return 0;
        }

        case WM_APP_THUMB_READY: {
            ThumbResult* r = (ThumbResult*)wp;
            if (!r) return 0;
            if (r->generation != g_gen.load()) {
                DeleteObject(r->bmp);
                delete r;
                return 0;
            }
            int itemCount = ListView_GetItemCount(g.hList);
            if (r->itemIndex >= 0 && r->itemIndex < itemCount) {
                int imgIdx = ImageList_Add(g.hThumbList, r->bmp, nullptr);
                if (imgIdx >= 0) {
                    LVITEMW lvi = {};
                    lvi.mask = LVIF_IMAGE;
                    lvi.iItem = r->itemIndex;
                    lvi.iImage = imgIdx;
                    ListView_SetItem(g.hList, &lvi);
                }
            }
            auto it = g.cache.find(r->path);
            if (it != g.cache.end()) {
                if (it->second.bmp) DeleteObject(it->second.bmp);
                if (it->second.hasBig && it->second.big) DeleteObject(it->second.big);
                g.cache.erase(it);
            }
            AppState::CacheEnt ent = {};
            ent.bmp = r->bmp;
            ent.hasBig = false;
            ent.big = nullptr;
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExW(r->path.c_str(), GetFileExInfoStandard, &fad))
                ent.ft = fad.ftLastWriteTime;
            g.cache[r->path] = ent;

            delete r;
            return 0;
        }

        case WM_APP_PREVIEW_READY: {
            PreviewResult* r = (PreviewResult*)wp;
            if (!r) return 0;
            if (r->generation != g_previewGen.load()) {
                DeleteObject(r->bmp);
                delete r;
                return 0;
            }
            StoreBigPreviewInCache(r->path, r->bmp); // cache przejmuje bitmapę
            if (g.hPreview && IsWindow(g.hPreview) &&
                g.previewIdx >= 0 && g.previewIdx < (int)g.currentPaths.size() &&
                g.currentPaths[g.previewIdx] == r->path) {
                HBITMAP copy = (HBITMAP)CopyImage(r->bmp, IMAGE_BITMAP, 0, 0, 0);
                SetCanvasBitmap(g.hPreviewCanvas, copy);
            }
            delete r;
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR nh = (LPNMHDR)lp;
            if (nh->idFrom == ID_LIST && nh->code == NM_DBLCLK) {
                LPNMITEMACTIVATE nia = (LPNMITEMACTIVATE)lp;
                if (nia->iItem >= 0) OnListDoubleClick(nia->iItem);
                return 0;
            }
            if (nh->idFrom == ID_LIST && nh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW nmlv = (LPNMLISTVIEW)lp;
                if ((nmlv->uChanged & LVIF_STATE) &&
                    (nmlv->uNewState & LVIS_SELECTED) &&
                    !(nmlv->uOldState & LVIS_SELECTED) &&
                    nmlv->iItem >= 0 && nmlv->iItem < (int)g.currentPaths.size()) {
                    UpdateMetaPanel(g.currentPaths[nmlv->iItem]);
                }
                return 0;
            }
            if (nh->idFrom == ID_TREE) {
                // POPRAWKA #1: zwolnienie std::wstring* z lParam przy kasowaniu.
                if (nh->code == TVN_DELETEITEM) {
                    LPNMTREEVIEWW nmtv = (LPNMTREEVIEWW)lp;
                    if (nmtv->itemOld.lParam) {
                        std::wstring* pstr = (std::wstring*)nmtv->itemOld.lParam;
                        delete pstr;
                    }
                    return 0;
                }
                if (nh->code == TVN_ITEMEXPANDEDW) {
                    LPNMTREEVIEWW ntv = (LPNMTREEVIEWW)lp;
                    if (ntv->action == TVE_EXPAND) {
                        ExpandTreeNode(g.hTree, ntv->itemNew.hItem);
                    }
                }
                if (nh->code == TVN_SELCHANGEDW) {
                    LPNMTREEVIEWW ntv = (LPNMTREEVIEWW)lp;
                    std::wstring* p = (std::wstring*)ntv->itemNew.lParam;
                    if (p) {
                        DWORD attr = GetFileAttributesW(p->c_str());
                        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                            g.currentFolder = *p;
                            g.settings.lastFolder = *p;
                            LoadThumbnails(g.hList, g.currentFolder, false);
                            UpdateMetaPanel(L"");
                        }
                    }
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == ID_REFRESH_BTN && (code == BN_CLICKED || code == 0)) {
                RefreshCurrentFolder();
                return 0;
            }
            if (id == ID_BROWSE_BTN && (code == BN_CLICKED || code == 0)) {
                BrowseForFolder();
                return 0;
            }
            if (id == ID_SORT_COMBO && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g.hSortCombo, CB_GETCURSEL, 0, 0);
                if (sel == 0) ApplySortMode(L"name");
                else if (sel == 1) ApplySortMode(L"date");
                else if (sel == 2) ApplySortMode(L"size");
                return 0;
            }
            if (id == ID_FILTER_COMBO && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g.hFilterCombo, CB_GETCURSEL, 0, 0);
                if (sel == 0) ApplyFilterMode(L"all");
                else if (sel == 1) ApplyFilterMode(L"tiff");
                else if (sel == 2) ApplyFilterMode(L"raw");
                else if (sel == 3) ApplyFilterMode(L"jpg");
                return 0;
            }
            // Komendy menu / skrótów
            if (id == IDM_ABOUT)     { ShowAboutWindow(hwnd); return 0; }
            if (id == IDM_SHORTCUTS) { ShowShortcutsStandalone(hwnd); return 0; }
            if (id == IDM_EXIT)      { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
            if (id == IDM_OPEN_LABSIM) {
                int sel = ListView_GetNextItem(g.hList, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)g.currentPaths.size())
                    OpenInLabSim(g.currentPaths[sel]);
                return 0;
            }
            if (id == IDM_SORT_NAME)   { ApplySortMode(L"name"); return 0; }
            if (id == IDM_SORT_DATE)   { ApplySortMode(L"date"); return 0; }
            if (id == IDM_SORT_SIZE)   { ApplySortMode(L"size"); return 0; }
            if (id == IDM_FILTER_ALL)  { ApplyFilterMode(L"all"); return 0; }
            if (id == IDM_FILTER_RAW)  { ApplyFilterMode(L"raw"); return 0; }
            if (id == IDM_FILTER_JPG)  { ApplyFilterMode(L"jpg"); return 0; }
            if (id == IDM_FILTER_TIFF) { ApplyFilterMode(L"tiff"); return 0; }
            if (id >= IDM_LANG_BASE && id < IDM_LANG_BASE + (int)g_langs.size()) {
                SwitchLanguage(g_langs[id - IDM_LANG_BASE].code);
                return 0;
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wp == VK_F1)  { ShowAboutWindow(hwnd); return 0; }
            if (wp == VK_F2)  { ShowShortcutsStandalone(hwnd); return 0; }
            if (wp == VK_F5)  { RefreshCurrentFolder(); return 0; }
            if (wp == VK_ESCAPE) {
                if (g.hPreview && IsWindow(g.hPreview)) { ClosePreviewWindow(); return 0; }
            }
            break;
        }

        case WM_CLOSE: {
            // POPRAWKA #3: sygnalizuj zamknięcie i poczekaj na wątek roboczy.
            g_shuttingDown.store(true);
            g_jobCV.notify_all();
            if (g_workerThread.joinable()) {
                g_workerThread.join();
            }

            g.settings.lastFolder = g.currentFolder;
            SaveSettings(g.settingsPath, g.settings);

            for (auto& kv : g.cache) {
                if (kv.second.bmp)  DeleteObject(kv.second.bmp);
                if (kv.second.hasBig && kv.second.big) DeleteObject(kv.second.big);
            }
            g.cache.clear();
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// =============================================================================
// PUNKT WEJSCIA
// =============================================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    g.hInst = hInst;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitGdiplus();
    InitSystemFont();

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES |
                ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    g.exeDir = GetExeDir();
    g.settingsPath = JoinPath(g.exeDir, L"lab_view_settings.json");
    g.settings = LoadSettings(g.settingsPath);
    InitLanguages(g.exeDir, g.settings.language);
    g.settings.language = g_langCode;

    // POPRAWKA #3: start trwałego wątku roboczego (thumb + preview).
    g_workerThread = std::thread(WorkerThreadProc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"LabViewMain";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);

    wchar_t title[96];
    _snwprintf_s(title, _countof(title), _TRUNCATE,
                 L"Lab_view  v%s  (build %s)", APP_VERSION, APP_BUILD);

    HWND hwnd = CreateWindowExW(
        0, L"LabViewMain", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 720,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        g_shuttingDown.store(true);
        g_jobCV.notify_all();
        if (g_workerThread.joinable()) g_workerThread.join();
        ShutdownGdiplus();
        CoUninitialize();
        return 1;
    }

    // POPRAWKA #5: fallback 20 -> 19.
    ApplyDarkMode(hwnd, IsSystemDarkMode());

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (!g.settings.lastFolder.empty()) {
        DWORD attr = GetFileAttributesW(g.settings.lastFolder.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            g.currentFolder = g.settings.lastFolder;
            LoadThumbnails(g.hList, g.currentFolder, false);
        }
    }

    ACCEL accel[] = {
        { FVIRTKEY,             VK_F1,   IDM_ABOUT },
        { FVIRTKEY,             VK_F2,   IDM_SHORTCUTS },
        { FVIRTKEY,             VK_F5,   ID_REFRESH_BTN },
        { FVIRTKEY | FCONTROL,  'O',     ID_BROWSE_BTN },
        { FVIRTKEY | FCONTROL,  'L',     IDM_OPEN_LABSIM },
        { FVIRTKEY | FCONTROL,  'S',     IDM_SORT_NAME },
        { FVIRTKEY | FCONTROL,  'D',     IDM_SORT_DATE },
        { FVIRTKEY | FCONTROL,  'Z',     IDM_SORT_SIZE },
        { FVIRTKEY | FCONTROL,  'A',     IDM_FILTER_ALL },
        { FVIRTKEY | FCONTROL,  '1',     IDM_FILTER_RAW },
        { FVIRTKEY | FCONTROL,  '2',     IDM_FILTER_JPG },
        { FVIRTKEY | FCONTROL,  '3',     IDM_FILTER_TIFF },
    };
    HACCEL hAccel = CreateAcceleratorTableW(accel, _countof(accel));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g.hPreview && IsWindow(g.hPreview) &&
            (msg.hwnd == g.hPreview || IsChild(g.hPreview, msg.hwnd))) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (hAccel) DestroyAcceleratorTable(hAccel);

    // POPRAWKA #3: bezpieczne domknięcie wątku, gdyby WM_CLOSE nie zadziałało.
    g_shuttingDown.store(true);
    g_jobCV.notify_all();
    if (g_workerThread.joinable()) g_workerThread.join();

    if (t_wicFactory) { t_wicFactory->Release(); t_wicFactory = nullptr; }
    if (g.hThumbList) { ImageList_Destroy(g.hThumbList); g.hThumbList = nullptr; }
    if (g_hFont) DeleteObject(g_hFont);
    ShutdownGdiplus();
    CoUninitialize();
    return (int)msg.wParam;
}
