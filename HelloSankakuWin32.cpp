// ダニエル・オズユルト
// 2025年1月7日

#include "Resource.h"
#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct InputState {
  bool leftArrow;
  bool rightArrow;
  bool upArrow;
  bool downArrow;

  POINT mousePosition;
  bool leftMouseButton;
  bool rightMouseButton;

  bool gamepadA;
  bool gamepadB;
  SHORT gamepadLX;
  SHORT gamepadLY;
};

// VRAM
constexpr auto VRAM_WIDTH = 1024;
constexpr auto VRAM_HEIGHT = 512;
constexpr auto VRAM_RESOLUTION = VRAM_WIDTH * VRAM_HEIGHT;
static UINT16 g_vram[VRAM_RESOLUTION];
constexpr auto VRAM_SIZE = sizeof(g_vram);

constexpr auto FRAME_WIDTH = 320;
constexpr auto FRAME_HEIGHT = 240;
constexpr auto FRAME_RESOLUTION = FRAME_WIDTH * FRAME_HEIGHT;
constexpr auto FRAME_OFFSET = VRAM_WIDTH * FRAME_HEIGHT;
static UINT16 (*g_currentFrame)[FRAME_RESOLUTION] =
    (UINT16(*)[FRAME_RESOLUTION])g_vram;

static InputState g_inputState;
static POINT g_position;
static HINSTANCE g_instance;
static constexpr auto MAX_LOADSTRING = 100;
static WCHAR g_title[MAX_LOADSTRING];
static WCHAR g_windowClassName[MAX_LOADSTRING];

static inline auto Win32GetMillisecs() -> long long;
static inline auto Win32RegisterClass(HINSTANCE hInstance) -> ATOM;
static auto CALLBACK Win32OnMsg(HWND, UINT, WPARAM, LPARAM) -> LRESULT;
static auto CALLBACK Win32OnAbout(HWND, UINT, WPARAM, LPARAM) -> INT_PTR;
static inline void Win32GetMouseXY(HWND hWnd);
static inline void Win32GetInputState(HWND hWnd);

static constexpr auto PackHighColor(int red, int green, int blue) -> UINT16;
static inline void ClearFramebuffer(int red, int green, int blue);
static constexpr void DrawTriangle(const POINT (&points)[3],
                                   const UINT16 (&highColors)[3]);

static constexpr auto Clamp(LONGLONG a, LONGLONG low,
                            LONGLONG high) -> LONGLONG;
static constexpr auto ToQ12(LONGLONG a) -> LONGLONG;
static constexpr auto Q12Mul(LONGLONG a, LONGLONG b) -> LONGLONG;
static constexpr auto Q12Div(LONGLONG a, LONGLONG b) -> LONGLONG;
static constexpr auto operator-(const POINT& a, const POINT& b) -> POINT;
static constexpr auto operator^(const POINT& a, const POINT& b) -> LONGLONG;

static inline void OnUpdate(long long dt);
static inline void OnRender();

auto APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                       _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine,
                       _In_ int nCmdShow) -> int {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

  LoadStringW(hInstance, IDS_APP_TITLE, g_title, MAX_LOADSTRING);
  LoadStringW(hInstance, IDC_HELLOSANKAKUWIN32, g_windowClassName,
              MAX_LOADSTRING);
  Win32RegisterClass(hInstance);

  g_instance = hInstance;

  RECT windowRect = {0, 0, FRAME_WIDTH * 2, FRAME_HEIGHT * 2};
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, TRUE);

  int totalWidth = windowRect.right - windowRect.left;
  int totalHeight = windowRect.bottom - windowRect.top;

  HWND hWnd = CreateWindowW(g_windowClassName, g_title, WS_OVERLAPPEDWINDOW, 0,
                            0, totalWidth, totalHeight, nullptr, nullptr,
                            hInstance, nullptr);

  if (hWnd == nullptr) {
    return FALSE;
  }

  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  HACCEL hAccelTable =
      LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_HELLOSANKAKUWIN32));

  HDC displayDC = GetDC(hWnd);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = FRAME_WIDTH;
  bmi.bmiHeader.biHeight = -FRAME_HEIGHT;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 16;
  bmi.bmiHeader.biCompression = BI_RGB;

  MSG msg;
  for (;;) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        return TRUE;
      }
      if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }

    static auto simulationTime = Win32GetMillisecs();
    auto const realTime = Win32GetMillisecs();

    Win32GetInputState(hWnd);

    constexpr auto maxUpdates = 5;
    constexpr auto dt = 16;
    constexpr auto maxDelta = 5 * dt;
    auto const delta = realTime - simulationTime;

    if (delta > maxDelta) {
      simulationTime = realTime - dt;
    }

    int updateCount = 0;

    while (simulationTime < realTime && updateCount < maxUpdates) {
      simulationTime += dt;

      OnUpdate(dt);

      ++updateCount;
    }

    static auto currentFrameIndex = 0;
    static UINT16(*frames[2])[FRAME_RESOLUTION] = {
        (UINT16(*)[FRAME_RESOLUTION])(g_vram),
        (UINT16(*)[FRAME_RESOLUTION])(g_vram + FRAME_OFFSET)};
    g_currentFrame = frames[currentFrameIndex];

    OnRender();

    currentFrameIndex ^= 1;
    RECT clientRect = {};
    GetClientRect(hWnd, &clientRect);
    auto const clientWidth = clientRect.right - clientRect.left;
    auto const clientHeight = clientRect.bottom - clientRect.top;
    StretchDIBits(displayDC, 0, 0, clientWidth, clientHeight, 0, 0, FRAME_WIDTH,
                  FRAME_HEIGHT, *g_currentFrame, &bmi, DIB_RGB_COLORS, SRCCOPY);

    auto const elapsed = Win32GetMillisecs() - realTime;
    if (elapsed < dt) {
      auto const sleepDuration = dt - elapsed;
      Sleep(sleepDuration);
    }
  }

  ReleaseDC(hWnd, displayDC);

  return (int)msg.wParam;
}

static inline auto Win32RegisterClass(HINSTANCE hInstance) -> ATOM {
  WNDCLASSEXW wcex = {};
  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wcex.lpfnWndProc = Win32OnMsg;
  wcex.cbClsExtra = 0;
  wcex.cbWndExtra = 0;
  wcex.hInstance = hInstance;
  wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_HELLOSANKAKUWIN32));
  wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + BLACK_BRUSH);
  wcex.lpszMenuName = MAKEINTRESOURCEW(IDI_HELLOSANKAKUWIN32);
  wcex.lpszClassName = g_windowClassName;
  wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

  return RegisterClassExW(&wcex);
}

static auto CALLBACK Win32OnMsg(HWND hWnd, UINT message, WPARAM wParam,
                                LPARAM lParam) -> LRESULT {
  switch (message) {
    case WM_COMMAND: {
      int wmId = LOWORD(wParam);
      switch (wmId) {
        case IDM_ABOUT:
          DialogBox(g_instance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd,
                    Win32OnAbout);
          break;
        case IDM_EXIT:
          DestroyWindow(hWnd);
          break;
        default:
          return DefWindowProc(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_DESTROY:
      PostQuitMessage(0);
      break;
    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
  }
  return 0;
}

static auto CALLBACK Win32OnAbout(HWND hDlg, UINT message, WPARAM wParam,
                                  LPARAM lParam) -> INT_PTR {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
    case WM_INITDIALOG:
      return (INT_PTR)TRUE;

    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return (INT_PTR)TRUE;
      }
      break;
  }
  return (INT_PTR)FALSE;
}

static inline auto Win32GetMillisecs() -> long long {
  static auto const freq = []() {
    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    return freq.QuadPart;
  }();
  LARGE_INTEGER counter = {};
  QueryPerformanceCounter(&counter);
  // T = 1 / f
  return (counter.QuadPart * 1'000) / freq;
}

static void OnUpdate(long long dt) {
  g_position.x = g_inputState.mousePosition.x;
  g_position.y = g_inputState.mousePosition.y;

  if (g_position.x < 0) {
    g_position.x = 0;
  }
  if (g_position.y < 0) {
    g_position.y = 0;
  }
  if (g_position.x > FRAME_WIDTH - 50) {
    g_position.x = FRAME_WIDTH - 50;
  }
  if (g_position.y > FRAME_HEIGHT - 50) {
    g_position.y = FRAME_HEIGHT - 50;
  }
}

static constexpr auto PackHighColor(int red, int green, int blue) -> UINT16 {
  return (((red & 0b11111) << 10) | ((green & 0b11111) << 5) |
          ((blue & 0b11111) << 0));
}

static inline void ClearFramebuffer(int red, int green, int blue) {
  for (auto i = 0; i < FRAME_RESOLUTION; ++i) {
    (*g_currentFrame)[i] = PackHighColor(red, green, blue);
  }
}

static inline void Win32GetMouseXY(HWND hWnd) {
  POINT pt = {};
  GetCursorPos(&pt);
  ScreenToClient(hWnd, &pt);

  RECT clientRect = {};
  GetClientRect(hWnd, &clientRect);
  auto const clientWidth = clientRect.right - clientRect.left;
  auto const clientHeight = clientRect.bottom - clientRect.top;

  //  x' = x * (Fw / Cw)
  //  y' = y * (Fh / Ch)
  g_inputState.mousePosition.x =
      (LONG)((float)pt.x * ((float)FRAME_WIDTH / (float)clientWidth));
  g_inputState.mousePosition.y =
      (LONG)((float)pt.y * ((float)FRAME_HEIGHT / (float)clientHeight));
}

static inline void Win32GetInputState(HWND hWnd) {
  g_inputState.leftArrow = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
  g_inputState.rightArrow = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
  g_inputState.upArrow = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
  g_inputState.downArrow = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;

  g_inputState.leftMouseButton = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  g_inputState.rightMouseButton = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

  Win32GetMouseXY(hWnd);
}

static inline void OnRender() {
  ClearFramebuffer(3, 16, 3);

  g_position.x = g_inputState.mousePosition.x - 25;
  g_position.y = g_inputState.mousePosition.y + 16;

  const POINT points[3] = {{g_position.x + 50, g_position.y},
                           {g_position.x, g_position.y},
                           {g_position.x + 25, g_position.y - 50}};
  constexpr UINT16 colors[3] = {PackHighColor(31, 0, 0),
                                PackHighColor(0, 31, 0),
                                PackHighColor(0, 0, 31)};
  DrawTriangle(points, colors);
}

static constexpr auto Clamp(LONGLONG a, LONGLONG low,
                            LONGLONG high) -> LONGLONG {
  return min(max(a, low), high);
}

static constexpr auto ToQ12(LONGLONG a) -> LONGLONG { return a << 12; }

static constexpr auto Q12Mul(LONGLONG a, LONGLONG b) -> LONGLONG {
  return (a * b) >> 12;
}

static constexpr auto Q12Div(LONGLONG a, LONGLONG b) -> LONGLONG {
  return (a << 12) / b;
}

static constexpr auto operator-(const POINT& a, const POINT& b) -> POINT {
  return {a.x - b.x, a.y - b.y};
}

static constexpr auto operator^(const POINT& a, const POINT& b) -> LONGLONG {
  return (LONGLONG)a.x * b.y - (LONGLONG)a.y * b.x;
}

static constexpr void DrawTriangle(const POINT (&points)[3],
                                   const UINT16 (&highColors)[3]) {
  auto const [va, vb, vc] = points;
  auto const [ca, cb, cc] = highColors;

  auto const [ax, ay] = va;
  auto const [bx, by] = vb;
  auto const [cx, cy] = vc;

  auto const xMin = Clamp(min(min(ax, bx), cx), 0, FRAME_WIDTH);
  auto const yMin = Clamp(min(min(ay, by), cy), 0, FRAME_HEIGHT);
  auto const xMax = Clamp(max(max(ax, bx), cx), 0, FRAME_WIDTH);
  auto const yMax = Clamp(max(max(ay, by), cy), 0, FRAME_HEIGHT);

  auto const ab = vb - va;
  auto const ac = vc - va;

  auto const abc = ab ^ ac;

  if (abc == 0) {
    return;
  }

  for (auto y = yMin; y <= yMax; ++y) {
    for (auto x = xMin; x <= xMax; ++x) {
      auto const p = POINT(x, y);
      auto const ap = p - va;
      auto const apc = ap ^ ac;
      auto const abp = ab ^ ap;

      auto const fixedABC = ToQ12(abc);
      auto const fixedAPC = ToQ12(apc);
      auto const fixedABP = ToQ12(abp);

      auto const beta = Q12Div(fixedAPC, fixedABC);
      auto const gamma = Q12Div(fixedABP, fixedABC);
      constexpr auto fixedOne = ToQ12(1);
      auto const alpha = fixedOne - beta - gamma;

      if (alpha < 0 || beta < 0 || gamma < 0) {
        continue;
      }

      auto const red = (alpha * cc) >> 12;
      auto const green = (beta * cc) >> 12;
      auto const blue = (gamma * cc) >> 12;
      auto const clampedRed = Clamp(red, 0, 31);
      auto const clampedGreen = Clamp(green, 0, 31);
      auto const clampedBlue = Clamp(blue, 0, 31);

      auto const offset = FRAME_WIDTH * y + x;
      (*g_currentFrame)[offset] =
          PackHighColor(clampedRed, clampedGreen, clampedBlue);
    }
  }
}
