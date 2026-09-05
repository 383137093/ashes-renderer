#include "GenericWindow.h"
#include <mutex>
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <windows.h>
#include <windowsx.h>

namespace Ashes {

//==============================================================================
// PlatformMenu
//==============================================================================

class PlatformMenu
{
public:

    PlatformMenu();
    PlatformMenu(const PlatformMenu&) = delete;
    ~PlatformMenu();
    PlatformMenu& operator = (const PlatformMenu&) = delete;
    
    bool IsAlive() const;
    void Create(HWND owner, std::shared_ptr<GenericMenuParams> menu_params);
    void Destroy();
    void ProcessMessage(HMENU hmenu, UINT msg, int pos);

private:
    
    void Build(std::shared_ptr<GenericMenuParams> menu_params);
    void OnMessageInitMenuPopup();
    void OnMessageMenuCommand(int pos);

    std::shared_ptr<GenericMenuParams>         menu_params_;
    HMENU                                      native_handle_ = NULL;
    std::vector<std::unique_ptr<PlatformMenu>> sub_menus_;
};

PlatformMenu::PlatformMenu()
{
}

PlatformMenu::~PlatformMenu()
{
    Destroy();
}

bool PlatformMenu::IsAlive() const
{
    bool alive = ::IsMenu(native_handle_);
    assert(alive || sub_menus_.empty());
    return alive;
}

void PlatformMenu::Create(
    HWND owner,
    std::shared_ptr<GenericMenuParams> menu_params)
{
    if (!IsAlive())
    {
        menu_params_ = nullptr;
        native_handle_ = ::CreateMenu();
        Build(menu_params);
        ::SetMenu(owner, native_handle_);
    }
}

void PlatformMenu::Destroy()
{
    if (IsAlive())
    {
        sub_menus_.clear();
        ::DestroyMenu(native_handle_);
        native_handle_ = NULL;
    }
}

void PlatformMenu::ProcessMessage(HMENU hmenu, UINT msg, int pos)
{
    if (hmenu == native_handle_)
    {
        switch (msg)
        {
            case WM_INITMENUPOPUP: OnMessageInitMenuPopup();  break;
            case WM_MENUCOMMAND:   OnMessageMenuCommand(pos); break;
        }
    }
    else
    {
        for (const std::unique_ptr<PlatformMenu>& sub_menu : sub_menus_)
            sub_menu->ProcessMessage(hmenu, msg, pos);
    }
}

void PlatformMenu::Build(std::shared_ptr<GenericMenuParams> menu_params)
{
    assert(menu_params_ == nullptr);
    assert(::IsMenu(native_handle_));
    assert(sub_menus_.empty());

    menu_params_ = menu_params;
    MENUINFO menu_info = {sizeof(MENUINFO)};
    menu_info.fMask = MIM_STYLE;
    menu_info.dwStyle = MNS_NOTIFYBYPOS;
    ::SetMenuInfo(native_handle_, &menu_info);

    for (const GenericMenuItemParamsVariant& item_params : menu_params_->items)
    {
        if (auto* btn = item_params.As<GenericMenuButtonParams>())
        {
            bool is_separator = (btn->text == kGenericMenuSeparatorText);
            UINT flags = (is_separator ? MF_SEPARATOR : MF_STRING);
            ::AppendMenuA(native_handle_, flags, 0, btn->text.data());
        }
        else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>())
        {
            for (const std::string& text : btns->texts)
            {
                bool is_separator = (text == kGenericMenuSeparatorText);
                UINT flags = (is_separator ? MF_SEPARATOR : MF_STRING);
                ::AppendMenuA(native_handle_, flags, 0, text.data());
            }
        }
        else if (auto* pop_btn = item_params.As<GenericMenuPopupButtonParams>())
        {
            auto sub_menu = std::make_unique<PlatformMenu>();
            sub_menu->native_handle_ = ::CreatePopupMenu();
            sub_menu->Build(pop_btn->menu_params);
            UINT_PTR id = (UINT_PTR)(sub_menu->native_handle_);
            ::AppendMenuA(native_handle_, MF_POPUP, id, pop_btn->text.data());
            sub_menus_.push_back(std::move(sub_menu));
        }
    }
}

void PlatformMenu::OnMessageInitMenuPopup()
{
    MENUITEMINFO item_info = {sizeof(MENUITEMINFO)};
    item_info.fMask = MIIM_STATE;

    for (const GenericMenuItemParamsVariant& item_params : menu_params_->items)
    {
        if (auto* btn = item_params.As<GenericMenuButtonParams>())
        {
            item_info.fState = (btn->is_checked() ? MFS_CHECKED : 0);
            ::SetMenuItemInfo(native_handle_, item_info.wID++, TRUE, &item_info);
        }
        else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>())
        {
            const int selection = btns->get_selection();
            for (int i = 0; i < static_cast<int>(btns->texts.size()); ++i)
            {
                item_info.fState = (i == selection ? MFS_CHECKED : 0);
                ::SetMenuItemInfo(native_handle_, item_info.wID++, TRUE, &item_info);
            }
        }
        else if (item_params.As<GenericMenuPopupButtonParams>())
        {
            ++item_info.wID;
        }
    }
}

void PlatformMenu::OnMessageMenuCommand(int pos)
{
    auto [item_params, sub_idx] = menu_params_->FindItem(pos);

    if (auto* btn = item_params->As<GenericMenuButtonParams>())
    {
        btn->on_clicked();
    }
    else if (auto* btns = item_params->As<GenericMenuButtonGroupParams>())
    {
        btns->on_clicked(sub_idx == btns->get_selection() ? -1 : sub_idx);
    }
}

//==============================================================================
// PlatformWindow
//==============================================================================

class PlatformWindow
{
public:

    PlatformWindow();
    PlatformWindow(const PlatformWindow&) = delete;
    ~PlatformWindow();
    PlatformWindow& operator = (const PlatformWindow&) = delete;

    bool IsAlive() const;
    void Create(const char* title, int width, int height);
    void Close();
    void SetTitle(const char* title);
    void BuildMenu(std::shared_ptr<GenericMenuParams> menu_params);
    bool RunMessageLoop();
    LRESULT ProcessMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    void CreateMemoryImage();
    void DestroyMemoryImage();
    void WriteMemoryImageMT(const float* rgb, int first, int last);
    void SubmitMemoryImageMT();
    void PresentMemoryImage();

    std::function<void()>             OnWindowClose;
    std::function<void(float, float)> OnMouseDragL;
    std::function<void(float, float)> OnMouseDragR;
    std::function<void(float)>        OnMouseWheel;

private:

    void OnMessageMouseMove(int x, int y);
    void OnMessageMouseWheel(int delta);
    void OnMessageClose();
    void OnMessageDestroy();

    SIZE                 size_ = {0, 0};
    HWND                 native_handle_ = NULL;
    PlatformMenu         menu_;
    HDC                  memory_dc_ = NULL;
    std::mutex           memory_image_mutex_;
    bool                 memory_image_dirty_ = false;
    HBITMAP              memory_image1_ = NULL;
    HBITMAP              memory_image2_ = NULL;
    std::uint8_t*        memory_image_data1_ = nullptr;
    std::uint8_t*        memory_image_data2_ = nullptr;
    std::optional<POINT> mouse_position_;
};

static LRESULT CALLBACK AshesWindowProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (auto* This = (PlatformWindow*)::GetPropA(hwnd, "AshesWindowPropThis"))
        return This->ProcessMessage(msg, wparam, lparam);
    return ::DefWindowProc(hwnd, msg, wparam, lparam);
}

PlatformWindow::PlatformWindow()
{
}

PlatformWindow::~PlatformWindow()
{
    assert(!IsAlive());
}

bool PlatformWindow::IsAlive() const
{
    bool alive = ::IsWindow(native_handle_);
    if (!alive)
    {
        assert(!menu_.IsAlive());
        assert(memory_dc_ == NULL);
        assert(memory_image1_ == NULL);
        assert(memory_image2_ == NULL);
        assert(memory_image_data1_ == nullptr);
        assert(memory_image_data2_ == nullptr);
    }
    return alive;
}

void PlatformWindow::Create(const char* title, int width, int height)
{
    if (!IsAlive())
    {
        constexpr DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT wnd_rect = {0, 0, width, height};
        ::AdjustWindowRect(&wnd_rect, style, TRUE);
        const int wnd_cx = wnd_rect.right - wnd_rect.left;
        const int wnd_cy = wnd_rect.bottom - wnd_rect.top;
        HWND hwnd = ::CreateWindowA("AshesWindowClass", title, style,
            CW_USEDEFAULT, CW_USEDEFAULT, wnd_cx, wnd_cy,
            NULL, NULL, ::GetModuleHandle(nullptr), this);
        assert(hwnd != NULL);
        size_ = {width, height};
        native_handle_ = hwnd;
        memory_image_dirty_ = false;
        mouse_position_.reset();
        ::SetPropA(hwnd, "AshesWindowPropThis", (HANDLE)(this));
        ::ShowWindow(hwnd, SW_SHOW);
    }
}

void PlatformWindow::SetTitle(const char* title)
{
    if (IsAlive())
    {
        ::SetWindowTextA(native_handle_, title);
    }
}

void PlatformWindow::Close()
{
    if (IsAlive())
    {
        ::PostMessage(native_handle_, WM_CLOSE, 0, 0);
    }
}

void PlatformWindow::BuildMenu(std::shared_ptr<GenericMenuParams> menu_params)
{
    if (IsAlive())
    {
        menu_.Destroy();
        menu_.Create(native_handle_, menu_params);
    }
}

bool PlatformWindow::RunMessageLoop()
{
    MSG msg = {};
    if (::GetMessage(&msg, NULL, 0, 0) > 0)
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) && msg.message != WM_QUIT)
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }
    return msg.message != WM_QUIT;
}

LRESULT PlatformWindow::ProcessMessage(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PresentMemoryImage();
            break;
        }
        case WM_CLOSE:
        {
            OnMessageClose();
            return S_OK;
        }
        case WM_DESTROY:
        {
            OnMessageDestroy();
            return S_OK;
        }
        case WM_INITMENUPOPUP:
        {
            menu_.ProcessMessage((HMENU)(wparam), msg, 0);
            return S_OK;
        }
        case WM_MENUCOMMAND:
        {
            menu_.ProcessMessage((HMENU)(lparam), msg, (int)(wparam));
            return S_OK;
        }
        case WM_MOUSEMOVE:
        {
            OnMessageMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return S_OK;
        }
        case WM_MOUSEWHEEL:
        {
            OnMessageMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam));
            return S_OK;
        }
    }
    return ::DefWindowProc(native_handle_, msg, wparam, lparam);
}

void PlatformWindow::CreateMemoryImage()
{
    if (IsAlive() && memory_dc_ == NULL)
    {
        HDC wnd_dc = ::GetDC(native_handle_);
        memory_dc_ = ::CreateCompatibleDC(wnd_dc);
        assert(memory_dc_ != NULL);
        ::ReleaseDC(native_handle_, wnd_dc);

        BITMAPINFOHEADER bmih = {sizeof(BITMAPINFOHEADER)};
        bmih.biWidth = size_.cx;
        bmih.biHeight = -size_.cy;
        bmih.biPlanes = 1;
        bmih.biBitCount = 24;
        bmih.biCompression = BI_RGB;

        memory_image1_ = ::CreateDIBSection(
            memory_dc_, (BITMAPINFO*)(&bmih), DIB_RGB_COLORS,
            (void**)(&memory_image_data1_), NULL, 0);
        assert(memory_image1_ != NULL);
        assert(memory_image_data1_ != nullptr);

        memory_image2_ = ::CreateDIBSection(
            memory_dc_, (BITMAPINFO*)(&bmih), DIB_RGB_COLORS,
            (void**)(&memory_image_data2_), NULL, 0);
        assert(memory_image2_ != NULL);
        assert(memory_image_data2_ != nullptr);
    }
}

void PlatformWindow::DestroyMemoryImage()
{
    if (memory_image1_ != NULL)
    {
        [[maybe_unused]] bool success = ::DeleteObject(memory_image1_);
        assert(success);
        memory_image1_ = NULL;
        memory_image_data1_ = nullptr;
    }

    if (memory_image2_ != NULL)
    {
        [[maybe_unused]] bool success = ::DeleteObject(memory_image2_);
        assert(success);
        memory_image2_ = NULL;
        memory_image_data2_ = nullptr;
    }

    if (memory_dc_ != NULL)
    {
        [[maybe_unused]] bool success = ::DeleteObject(memory_dc_);
        assert(success);
        memory_dc_ = NULL;
    }
}

void PlatformWindow::WriteMemoryImageMT(const float* rgb, int first, int last)
{
    if (memory_image_data1_ != nullptr)
    {
        auto Saturate = [](float f) { return std::clamp(f, 0.0f, 1.0f); };
        const float* src = rgb + 3 * first;
        std::uint8_t* dest = memory_image_data1_ + 3 * first;

        for (int n = last - first; n > 0; --n, src += 3, dest += 3)
        {
            dest[0] = static_cast<std::uint8_t>(Saturate(src[2]) * 255.0f);
            dest[1] = static_cast<std::uint8_t>(Saturate(src[1]) * 255.0f);
            dest[2] = static_cast<std::uint8_t>(Saturate(src[0]) * 255.0f);
        }
    }
}

void PlatformWindow::SubmitMemoryImageMT()
{
    // if memory image of previous frame is presenting, the lock will fail.
    // discarding the memory image of current frame is not a big problem.
    std::unique_lock<std::mutex> locker(
        memory_image_mutex_, std::try_to_lock);

    if (locker.owns_lock())
    {
        std::swap(memory_image1_, memory_image2_);
        std::swap(memory_image_data1_, memory_image_data2_);
        memory_image_dirty_ = true;
        ::PostMessage(native_handle_, WM_PAINT, 0, 0);
    }
}

void PlatformWindow::PresentMemoryImage()
{
    std::lock_guard<std::mutex> locker(memory_image_mutex_);
   
    if (memory_dc_ != NULL && memory_image_dirty_)
    {
        HDC wnd_dc = ::GetDC(native_handle_);
        HGDIOBJ original = ::SelectObject(memory_dc_, memory_image2_);
        ::BitBlt(wnd_dc, 0, 0, size_.cx, size_.cy, memory_dc_, 0, 0, SRCCOPY);
        ::SelectObject(memory_dc_, original);
        ::ReleaseDC(native_handle_, wnd_dc);
        memory_image_dirty_ = false;
    }
}

void PlatformWindow::OnMessageMouseMove(int x, int y)
{
    if (mouse_position_)
    {
        float delta_x = static_cast<float>(x - mouse_position_->x);
        float delta_y = static_cast<float>(y - mouse_position_->y);
        bool lbutton_pressed = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        bool rbutton_pressed = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000);
        if (lbutton_pressed && OnMouseDragL)
            OnMouseDragL(delta_x, delta_y);
        else if (rbutton_pressed && OnMouseDragR)
            OnMouseDragR(delta_x, delta_y);
    }
    mouse_position_ = {x, y};
}

void PlatformWindow::OnMessageMouseWheel(int delta)
{
    if (OnMouseWheel)
        OnMouseWheel(static_cast<float>(delta / WHEEL_DELTA));
}

void PlatformWindow::OnMessageClose()
{
    if (OnWindowClose)
        OnWindowClose();
    ::DestroyWindow(native_handle_);
}

void PlatformWindow::OnMessageDestroy()
{
    mouse_position_.reset();
    DestroyMemoryImage();
    menu_.Destroy();
    native_handle_ = NULL;
    ::PostQuitMessage(0);
}

//==============================================================================
// GenericWindow
//==============================================================================

void GenericWindow::InitializePlatform()
{
    // If you register the window class by using RegisterClassA, the application
    // tells the system that the windows of the created class expect
    // messages with text or character parameters to use the ANSI character set;
    // if you register it by using RegisterClassW, the application requests that
    // the system pass text parameters of messages as Unicode.
    WNDCLASSW wnd_class = {};
    wnd_class.style = CS_HREDRAW | CS_VREDRAW;
    wnd_class.lpfnWndProc = AshesWindowProc;
    wnd_class.hInstance = ::GetModuleHandle(nullptr);
    wnd_class.hIcon = ::LoadIcon(NULL, IDI_APPLICATION);
    wnd_class.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wnd_class.hbrBackground = (HBRUSH)::GetStockObject(WHITE_BRUSH);
    wnd_class.lpszClassName = L"AshesWindowClass";
    [[maybe_unused]] ATOM result = ::RegisterClassW(&wnd_class);
    assert(result != NULL);
}

void GenericWindow::UnInitializePlatform()
{
}

std::filesystem::path GenericWindow::CurrentPath()
{
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::filesystem::path GenericWindow::ChooseModelFile()
{
    char filename[MAX_PATH + 5] = {0};
    ::GetCurrentDirectoryA(MAX_PATH, filename);
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ::GetForegroundWindow();
    ofn.lpstrFilter = "Model File\0*.obj;*.obj-ashes;*.gltf\0Any File\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select a Model File";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
    return ::GetOpenFileNameA(&ofn) ? filename : "";
}

GenericWindow::GenericWindow()
{
    impl_ = new PlatformWindow;
}

GenericWindow::~GenericWindow()
{
    delete impl_;
}

void GenericWindow::Create(const char* title, int width, int height)
{
    impl_->Create(title, width, height);
}

void GenericWindow::Close()
{
    impl_->Close();
}

void GenericWindow::SetTitle(const char* title)
{
    impl_->SetTitle(title);
}

void GenericWindow::BuildMenu(std::shared_ptr<GenericMenuParams> menu_params)
{
    impl_->BuildMenu(menu_params);
}

bool GenericWindow::RunMessageLoop()
{
    return impl_->RunMessageLoop();
}

void GenericWindow::CreateMemoryImage()
{
    impl_->CreateMemoryImage();
}

void GenericWindow::WriteMemoryImageMT(const float* rgb, int first, int last)
{
    impl_->WriteMemoryImageMT(rgb, first, last);
}

void GenericWindow::SubmitMemoryImageMT()
{
    impl_->SubmitMemoryImageMT();
}

void GenericWindow::SetOnWindowClose(std::function<void()> func)
{
    impl_->OnWindowClose = std::move(func);
}

void GenericWindow::SetOnMouseDragL(std::function<void(float, float)> func)
{
    impl_->OnMouseDragL = std::move(func);
}

void GenericWindow::SetOnMouseDragR(std::function<void(float, float)> func)
{
    impl_->OnMouseDragR = std::move(func);
}

void GenericWindow::SetOnMouseWheel(std::function<void(float)> func)
{
    impl_->OnMouseWheel = std::move(func);
}

}