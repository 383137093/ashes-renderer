//==============================================================================
// Xlib
// 
// << The Xlib Manual >>
// https://tronche.com/gui/x/xlib/
// 
// << Xlib - C Language X Interface >>
// https://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.pdf
//==============================================================================

#include "GenericWindow.h"
#include <mutex>
#include <memory>
#include <vector>
#include <cassert>
#include <climits>
#include <cstdint>
#include <optional>
#include <algorithm>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace Ashes {

using ushort = unsigned short;

//==============================================================================
// PlatformMenu
//==============================================================================

enum class PlatformMenuDirection { Horizontal, Vertical, };

struct PlatformMenuInfo
{
    Display*              display = nullptr;
    Window                owner = None;
    PlatformMenuDirection dir = PlatformMenuDirection::Horizontal;
    XRectangle            rect = {0, 0, 0, 0};
    ushort                border_size = 1;
    ushort                item_height = 18;
    Font                  font = None;
    long                  highlight_color = 0;
};

struct PlatformMenuItemInfo
{
    std::string_view text;
    bool             is_checked = false;
    XRectangle       rect = {0, 0, 0, 0};
    XRectangle       radio_sub_rect = {0, 0, 0, 0};
    XPoint           text_offset = {0, 0};
};

class PlatformMenu
{
public:

    PlatformMenu();
    PlatformMenu(const PlatformMenu&) = delete;
    ~PlatformMenu();
    PlatformMenu& operator = (const PlatformMenu&) = delete;

    Window NativeHandle() const;
    bool IsAlive() const;
    void Create(std::shared_ptr<GenericMenuParams> menu_params,
        const PlatformMenuInfo& menu_info);
    void Destroy();
    void Show();
    void Hide();
    void ProcessEvent(const XEvent& event);

private:

    void InitializeItemInfos();
    XRectangle ArrangeItems(short x, short y);
    XRectangle ArrangeItemsHorizontally(short x, short y);
    XRectangle ArrangeItemsVertically(short x, short y);
    void CreateSubMenus();

    void UpdateItemsCheckState();
    void DrawItems() const;
    void DrawItem(GC gc, const PlatformMenuItemInfo& item_info,
        unsigned long background, unsigned long foreground) const;
    
    static ushort TextWidth(XFontStruct* font_info, std::string_view text);
    static ushort ItemPaddingX(XFontStruct* font_info);
    static ushort ItemTextOffsetY(XFontStruct* font_info, ushort item_height);

    void OnEventMotionNotify(short x, short y);
    void OnEventButton1Press(short x, short y);
    void OnEventLeaveNotify();

    const PlatformMenuItemInfo* FindItemByPoint(short x, short y) const;
    PlatformMenu* GetSubMenuOfItem(const PlatformMenuItemInfo& item_info) const;
    void HighlightItem(const PlatformMenuItemInfo* item_info);

private:

    std::shared_ptr<GenericMenuParams> menu_params_;
    PlatformMenuInfo                   menu_info_;
    std::vector<PlatformMenuItemInfo>  item_infos_;
    Window                             native_handle_ = None;
    const PlatformMenuItemInfo*        highlight_item_info_ = nullptr;
    std::unordered_map<int,
        std::unique_ptr<PlatformMenu>> sub_menus_;
};

PlatformMenu::PlatformMenu()
{
}

PlatformMenu::~PlatformMenu()
{
    Destroy();
}

Window PlatformMenu::NativeHandle() const
{
    return native_handle_;
}

bool PlatformMenu::IsAlive() const
{
    bool alive = (native_handle_ != None);
    if (!alive)
    {
        assert(item_infos_.empty());
        assert(highlight_item_info_ == nullptr);
        assert(sub_menus_.empty());
    }
    return alive;
}

void PlatformMenu::Create(
    std::shared_ptr<GenericMenuParams> menu_params,
    const PlatformMenuInfo& menu_info)
{
    if (!IsAlive())
    {
        menu_params_ = menu_params;
        menu_info_ = menu_info;
        InitializeItemInfos();

        XRectangle items_rect = ArrangeItems(0, 0);
        ushort width  = menu_info.rect.width;
        ushort height = menu_info.rect.height;
        if (width == 0)  { width  = items_rect.x + items_rect.width; }
        if (height == 0) { height = items_rect.y + items_rect.height; }

        int screen = ::XDefaultScreen(menu_info.display);
        unsigned long white = ::XWhitePixel(menu_info.display, screen);
        unsigned long black = ::XBlackPixel(menu_info.display, screen);

        native_handle_ = ::XCreateSimpleWindow(
            menu_info.display, menu_info.owner,
            menu_info.rect.x, menu_info.rect.y, width, height,
            menu_info.border_size, black, white);

        CreateSubMenus();
    }
}

void PlatformMenu::Destroy()
{
    if (IsAlive())
    {
        sub_menus_.clear();
        highlight_item_info_ = nullptr;
        ::XDestroyWindow(menu_info_.display, native_handle_);
        native_handle_ = None;
        item_infos_.clear();
    }
}

void PlatformMenu::Show()
{
    if (IsAlive() && !item_infos_.empty())
    {
        ::XMapWindow(menu_info_.display, native_handle_);
        UpdateItemsCheckState();
        DrawItems();
        ::XFlush(menu_info_.display);
    }
}

void PlatformMenu::Hide()
{
    if (IsAlive())
    {
        ::XUnmapWindow(menu_info_.display, native_handle_);
        ::XFlush(menu_info_.display);
    }
}

void PlatformMenu::ProcessEvent(const XEvent& event)
{
    if (event.xany.window == native_handle_)
    {
        if (event.type == MotionNotify)
        {
            OnEventMotionNotify(event.xmotion.x, event.xmotion.y);
        }
        else if (event.type == ButtonPress)
        {
            if (event.xbutton.button == Button1)
            {
                OnEventButton1Press(event.xbutton.x, event.xbutton.y);
            }
        }
        else if (event.type == LeaveNotify)
        {
            OnEventLeaveNotify();
        }
    }
    else
    {
        for (auto&& [_, sub_menu] : sub_menus_)
            sub_menu->ProcessEvent(event);
    }
}

void PlatformMenu::InitializeItemInfos()
{
    assert(item_infos_.empty());
    assert(sub_menus_.empty());
    PlatformMenuItemInfo item_info;

    for (const GenericMenuItemParamsVariant& item_params : menu_params_->items)
    {
        if (auto* btn = item_params.As<GenericMenuButtonParams>())
        {
            item_info.text = btn->text;
            item_infos_.push_back(item_info);
        }
        else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>())
        {
            for (std::string_view text : btns->texts)
            {
                item_info.text = text;
                item_infos_.push_back(item_info);
            }
        }
        else if (auto* pop_btn = item_params.As<GenericMenuPopupButtonParams>())
        {
            item_info.text = pop_btn->text;
            item_infos_.push_back(item_info);
            auto sub_menu = std::make_unique<PlatformMenu>();
            sub_menu->menu_params_ = pop_btn->menu_params;
            sub_menus_[item_infos_.size() - 1] = std::move(sub_menu);
        }
    }
}

XRectangle PlatformMenu::ArrangeItems(short x, short y)
{
    if (menu_info_.dir == PlatformMenuDirection::Horizontal)
    {
        return ArrangeItemsHorizontally(x, y);
    }
    else if (menu_info_.dir == PlatformMenuDirection::Vertical)
    {
        return ArrangeItemsVertically(x, y);
    }
    return {x, y, 0, 0};
}

XRectangle PlatformMenu::ArrangeItemsHorizontally(short x, short y)
{
    XFontStruct* font_info = ::XQueryFont(menu_info_.display, menu_info_.font);
    assert(font_info != nullptr);

    const ushort item_height = menu_info_.item_height;
    const ushort item_padding_x = ItemPaddingX(font_info);
    const ushort item_text_offset_y = ItemTextOffsetY(font_info, item_height);
    const ushort separator_margin_x = item_padding_x / 2;
    XRectangle bounds = {x, y, 0, item_height};

    for (PlatformMenuItemInfo& item_info : item_infos_)
    {   
        if (item_info.text != kGenericMenuSeparatorText)
        {
            ushort item_text_width = TextWidth(font_info, item_info.text);
            item_info.rect.x = bounds.x + bounds.width;
            item_info.rect.y = bounds.y;
            item_info.rect.width = 2 * item_padding_x + item_text_width;
            item_info.rect.height = bounds.height;
            item_info.text_offset.x = item_padding_x;
            item_info.text_offset.y = item_text_offset_y;
            item_info.radio_sub_rect = {0, 0, 0, 0};
            bounds.width += item_info.rect.width;
        }
        else
        {
            item_info.rect.x = bounds.x + bounds.width + separator_margin_x;
            item_info.rect.y = bounds.y + 2;
            item_info.rect.width = 1;
            item_info.rect.height = bounds.height - 4;
            item_info.text_offset = {0, 0};
            item_info.radio_sub_rect = {0, 0, 0, 0};
            bounds.width += 2 * separator_margin_x + item_info.rect.width;
        }
    }
    
    return bounds;
}

XRectangle PlatformMenu::ArrangeItemsVertically(short x, short y)
{
    XFontStruct* font_info = ::XQueryFont(menu_info_.display, menu_info_.font);
    assert(font_info != nullptr);

    const ushort item_height = menu_info_.item_height;
    const ushort item_padding_x = ItemPaddingX(font_info);
    const ushort radio_extent = item_height / 2;
    const ushort item_text_offset_x = 2 * item_padding_x + radio_extent;
    const ushort item_text_offset_y = ItemTextOffsetY(font_info, item_height);
    const ushort separator_margin_y = item_height / 3;

    ushort max_item_text_width = 0;
    for (const PlatformMenuItemInfo& item_info : item_infos_)
    {   
        if (item_info.text != kGenericMenuSeparatorText)
        {
            ushort item_text_width = TextWidth(font_info, item_info.text);
            max_item_text_width = std::max(max_item_text_width, item_text_width);
        }
    }

    XRectangle bounds = {x, y, 0, 0};
    bounds.width = item_text_offset_x + max_item_text_width + item_padding_x;
    bounds.height = separator_margin_y;

    for (PlatformMenuItemInfo& item_info : item_infos_)
    {   
        if (item_info.text != kGenericMenuSeparatorText)
        {
            item_info.rect.x = bounds.x;
            item_info.rect.y = bounds.y + bounds.height;
            item_info.rect.width = bounds.width;
            item_info.rect.height = item_height;
            item_info.text_offset.x = item_text_offset_x;
            item_info.text_offset.y = item_text_offset_y;
            item_info.radio_sub_rect.x = item_padding_x;
            item_info.radio_sub_rect.y = (item_height - radio_extent) / 2;
            item_info.radio_sub_rect.width = radio_extent;
            item_info.radio_sub_rect.height = radio_extent;
            bounds.height += item_info.rect.height;
        }
        else
        {
            item_info.rect.x = bounds.x + item_text_offset_x - 2;
            item_info.rect.y = bounds.y + bounds.height + separator_margin_y;
            item_info.rect.width = max_item_text_width + 4;
            item_info.rect.height = 1;
            item_info.text_offset = {0, 0};
            item_info.radio_sub_rect = {0, 0, 0, 0};
            bounds.height += 2 * separator_margin_y + item_info.rect.height;
        }
    }
    
    bounds.height += separator_margin_y;
    return bounds;
}

void PlatformMenu::CreateSubMenus()
{
    PlatformMenuInfo sub_menu_info = menu_info_;
    sub_menu_info.dir = PlatformMenuDirection::Vertical;
    sub_menu_info.rect = {0, 0, 0, 0};
    sub_menu_info.border_size = 1;

    for (auto&& [item_idx, sub_menu] : sub_menus_)
    {
        const PlatformMenuItemInfo& item_info = item_infos_[item_idx];
        sub_menu_info.rect.x = item_info.rect.x;
        sub_menu_info.rect.y = item_info.rect.y + item_info.rect.height + 1;
        sub_menu->Create(sub_menu->menu_params_, sub_menu_info);
    }
}

void PlatformMenu::UpdateItemsCheckState()
{
    PlatformMenuItemInfo* item_info = item_infos_.data();

    for (const GenericMenuItemParamsVariant& item_params : menu_params_->items)
    {
        if (auto* btn = item_params.As<GenericMenuButtonParams>())
        {
            (item_info++)->is_checked = btn->is_checked();
        }
        else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>())
        {
            const int selection = btns->get_selection();
            for (int i = 0; i < static_cast<int>(btns->texts.size()); ++i)
                (item_info++)->is_checked = (i == selection);
        }
        else if (item_params.As<GenericMenuPopupButtonParams>())
        {
            (item_info++)->is_checked = false;
        }
    }
}

void PlatformMenu::DrawItems() const
{
    int screen = ::XDefaultScreen(menu_info_.display);
    unsigned long white = ::XWhitePixel(menu_info_.display, screen);
    unsigned long black = ::XBlackPixel(menu_info_.display, screen);
    GC gc = ::XDefaultGC(menu_info_.display, screen);
    ::XSetFont(menu_info_.display, gc, menu_info_.font);

    for (const PlatformMenuItemInfo& item_info : item_infos_)
    {
        unsigned long background = (&item_info == highlight_item_info_ ?
            menu_info_.highlight_color : white);
        DrawItem(gc, item_info, background, black);
    }
}

void PlatformMenu::DrawItem(
    GC gc,
    const PlatformMenuItemInfo& item_info,
    unsigned long background,
    unsigned long foreground) const
{
    Display* display = menu_info_.display;
    Drawable d = native_handle_;
    const int x = item_info.rect.x;
    const int y = item_info.rect.y;
    const int w = item_info.rect.width;
    const int h = item_info.rect.height;

    // erase item background
    ::XSetBackground(display, gc, background);
    ::XSetForeground(display, gc, background);
    ::XFillRectangle(display, d, gc, x, y, w, h);
    ::XSetForeground(display, gc, foreground);

    if (item_info.text != kGenericMenuSeparatorText)
    {
        // draw item string
        if (!item_info.text.empty())
        {
            int sx = x + item_info.text_offset.x;
            int sy = y + item_info.text_offset.y;
            int length = static_cast<int>(item_info.text.size());
            ::XDrawString(display, d, gc, sx, sy, item_info.text.data(), length);
        }

        // draw radio button for checked item
        if (item_info.is_checked)
        {
            int rx = x + item_info.radio_sub_rect.x;
            int ry = y + item_info.radio_sub_rect.y;
            int rw = item_info.radio_sub_rect.width;
            int rh = item_info.radio_sub_rect.height;
            constexpr int angle2 = 64 * 360;  // in units of degrees * 64
            ::XDrawArc(display, d, gc, rx,     ry,     rw + 1, rh + 1, 0, angle2);
            ::XFillArc(display, d, gc, rx + 1, ry + 1, rw - 1, rh - 1, 0, angle2);
        }
    }
    else
    {
        ::XDrawLine(display, d, gc, x, y, x + w - 1, y + h - 1);
    }
}

ushort PlatformMenu::TextWidth(XFontStruct* font_info, std::string_view text)
{
    return static_cast<ushort>(::XTextWidth(
        font_info, text.data(), static_cast<int>(text.size())));
}

ushort PlatformMenu::ItemPaddingX(XFontStruct* font_info)
{
    // use the max character width of font as padding.
    return font_info->max_bounds.rbearing - font_info->min_bounds.lbearing;
}

ushort PlatformMenu::ItemTextOffsetY(XFontStruct* font_info, ushort item_height)
{
    // Y-bounds of the character is [y - ascent, y + descent].
    short ascent = font_info->max_bounds.ascent;
    short descent = font_info->max_bounds.descent;
    return ascent + (item_height - ascent - descent + 1) / 2;
}

void PlatformMenu::OnEventMotionNotify(short x, short y)
{
    HighlightItem(FindItemByPoint(x, y));
}

void PlatformMenu::OnEventButton1Press(short x, short y)
{
    if (const PlatformMenuItemInfo* item_info = FindItemByPoint(x, y))
    {
        int item_idx = static_cast<int>(item_info - item_infos_.data());
        auto [item_params, sub_idx] = menu_params_->FindItem(item_idx);

        if (auto* btn = item_params->As<GenericMenuButtonParams>())
        {
            btn->on_clicked();
            UpdateItemsCheckState();
            DrawItems();
        }
        else if (auto* btns = item_params->As<GenericMenuButtonGroupParams>())
        { 
            btns->on_clicked(sub_idx == btns->get_selection() ? -1 : sub_idx);
            UpdateItemsCheckState();
            DrawItems();
        }
    }
}

void PlatformMenu::OnEventLeaveNotify()
{
    HighlightItem(nullptr);
}

const PlatformMenuItemInfo* PlatformMenu::FindItemByPoint(short x, short y) const
{
    for (const PlatformMenuItemInfo& item_info : item_infos_)
    {   
        const XRectangle& r = item_info.rect;
        if (r.x <= x && x < r.x + r.width && r.y <= y && y < r.y + r.height)
            return &item_info;
    }
    return nullptr;
}

PlatformMenu* PlatformMenu::GetSubMenuOfItem(
    const PlatformMenuItemInfo& item_info) const
{
    int item_idx = static_cast<int>(&item_info - item_infos_.data());
    auto iter = sub_menus_.find(item_idx);
    return iter == sub_menus_.end() ? nullptr : iter->second.get();
}

void PlatformMenu::HighlightItem(const PlatformMenuItemInfo* item_info)
{
    if (item_info != highlight_item_info_)
    {
        int screen = ::XDefaultScreen(menu_info_.display);
        unsigned long white = ::XWhitePixel(menu_info_.display, screen);
        unsigned long black = ::XBlackPixel(menu_info_.display, screen);
        GC gc = ::XDefaultGC(menu_info_.display, screen);
        ::XSetFont(menu_info_.display, gc, menu_info_.font);

        if (highlight_item_info_ != nullptr)
        {
            PlatformMenu* sub_menu = GetSubMenuOfItem(*highlight_item_info_);
            if (sub_menu != nullptr) { sub_menu->Hide(); }
            DrawItem(gc, *highlight_item_info_, white, black);
        }

        if (item_info != nullptr)
        {
            PlatformMenu* sub_menu = GetSubMenuOfItem(*item_info);
            if (sub_menu != nullptr) { sub_menu->Show(); }
            DrawItem(gc, *item_info, menu_info_.highlight_color, black);
        }

        highlight_item_info_ = item_info;
    }
}

//==============================================================================
// PlatformWindow
//==============================================================================

struct PlatformWindowInfo
{
    Display* display = nullptr;
    XPoint   size = {0, 0};
    Font     font = None;
    long     highlight_color = 0;
};

class PlatformWindow
{
public:

    PlatformWindow();
    PlatformWindow(const PlatformWindow&) = delete;
    ~PlatformWindow();
    PlatformWindow& operator = (const PlatformWindow&) = delete;

    bool IsAlive() const;
    void Create(const PlatformWindowInfo& wnd_info);
    void Close();
    void SetTitle(const char* title);
    void BuildMenu(std::shared_ptr<GenericMenuParams> menu_params);
    bool RunMessageLoop();
    void ProcessEvent(const XEvent& event);

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

    static constexpr int kPixelSize = 4;
    static constexpr ushort kWindowMenuHeight = 20;
    static constexpr long kEventMask = (ExposureMask | EnterWindowMask |
        PointerMotionMask | ButtonPressMask | ButtonReleaseMask);

    template <typename EventType>
    bool ForwardSubWindowEvent(const EventType& event);
    void OnEventEnterNotify();
    void OnEventMotionNotify(short x, short y);
    void OnEventButtonPress(unsigned int button);
    void OnEventButtonRelease(unsigned int button);
    bool HandleWindowClose();

    PlatformWindowInfo               wnd_info_;
    Window                           native_handle_ = None;
    bool                             closed_ = false;
    PlatformMenu                     menu_;
    std::mutex                       memory_image_mutex_;
    bool                             memory_image_dirty_ = false;
    XImage*                          memory_image1_ = nullptr;
    XImage*                          memory_image2_ = nullptr;
    std::uint8_t*                    memory_image_data1_ = nullptr;
    std::uint8_t*                    memory_image_data2_ = nullptr;
    std::optional<XPoint>            mouse_position_;
    std::unordered_set<unsigned int> pressed_buttons_;
};

PlatformWindow::PlatformWindow()
{
}

PlatformWindow::~PlatformWindow()
{
    assert(!IsAlive());
}

bool PlatformWindow::IsAlive() const
{
    bool alive = (native_handle_ != None);
    if (!alive)
    {
        assert(!menu_.IsAlive());
        assert(memory_image1_ == nullptr);
        assert(memory_image2_ == nullptr);
        assert(memory_image_data1_ == nullptr);
        assert(memory_image_data2_ == nullptr);
    }
    return alive;
}

void PlatformWindow::Create(const PlatformWindowInfo& wnd_info)
{
    if (!IsAlive())
    {
        const ushort w = wnd_info.size.x;
        const ushort h = wnd_info.size.y + kWindowMenuHeight;
        int screen = ::XDefaultScreen(wnd_info.display);
        Window root = ::XRootWindow(wnd_info.display, screen);
        Window wnd = ::XCreateSimpleWindow(
            wnd_info.display, root, 0, 0, w, h, 1,
            ::XWhitePixel(wnd_info.display, screen),
            ::XBlackPixel(wnd_info.display, screen));
        assert(wnd != None);

        wnd_info_ = wnd_info;
        native_handle_ = wnd;
        closed_ = false;
        memory_image_dirty_ = false;
        mouse_position_.reset();
        pressed_buttons_.clear();

        XSizeHints* size_hints = ::XAllocSizeHints();
        size_hints->flags = PMinSize | PMaxSize;
        size_hints->min_width = w;
        size_hints->max_width = w;
        size_hints->min_height = h;
        size_hints->max_height = h;
        ::XSetWMNormalHints(wnd_info.display, wnd, size_hints);
        ::XFree(size_hints);

        ::XSelectInput(wnd_info.display, wnd, kEventMask);
        ::XMapWindow(wnd_info.display, wnd);
        ::XFlush(wnd_info.display);
    }
}

void PlatformWindow::Close()
{
    if (IsAlive() && !closed_)
    {
        if (OnWindowClose)
            OnWindowClose();
        closed_ = true;
    }
}

void PlatformWindow::SetTitle(const char* title)
{
    if (IsAlive())
    {
        ::XStoreName(wnd_info_.display, native_handle_, title);
    }
}

void PlatformWindow::BuildMenu(std::shared_ptr<GenericMenuParams> menu_params)
{
    if (IsAlive())
    {
        PlatformMenuInfo menu_info;
        menu_info.display = wnd_info_.display;
        menu_info.owner = native_handle_;
        menu_info.dir = PlatformMenuDirection::Horizontal;
        menu_info.rect.width = wnd_info_.size.x - 2;
        menu_info.border_size = 1;
        menu_info.item_height = kWindowMenuHeight - 2;
        menu_info.font = wnd_info_.font;
        menu_info.highlight_color = wnd_info_.highlight_color;
        menu_.Destroy();
        menu_.Create(menu_params, menu_info);
        menu_.Show();
    }
}

bool PlatformWindow::RunMessageLoop()
{
    if (IsAlive())
    {
        const Window wnd = native_handle_;
        XEvent event;
        ::XWindowEvent(wnd_info_.display, wnd, kEventMask, &event);
        ProcessEvent(event);
        while (::XCheckWindowEvent(wnd_info_.display, wnd, kEventMask, &event))
            ProcessEvent(event);
    }
    return !HandleWindowClose();
}

void PlatformWindow::ProcessEvent(const XEvent& event)
{
    if (event.type == GraphicsExpose) 
    {
        PresentMemoryImage();
    }
    else if (event.type == EnterNotify)
    {
        if (event.xcrossing.subwindow == None)
            OnEventEnterNotify();
    }
    else if (event.type == MotionNotify)
    {
        if (!ForwardSubWindowEvent(event.xmotion))
            OnEventMotionNotify(event.xmotion.x, event.xmotion.y);
    }
    else if (event.type == ButtonPress)
    {
        if (!ForwardSubWindowEvent(event.xbutton))
            OnEventButtonPress(event.xbutton.button);
    }
    else if (event.type == ButtonRelease)
    {
        if (!ForwardSubWindowEvent(event.xbutton))
            OnEventButtonRelease(event.xbutton.button);
    }
}

void PlatformWindow::CreateMemoryImage()
{
    if (IsAlive() && memory_image1_ == nullptr && memory_image2_ == nullptr)
    {
        constexpr int pad = 8 * kPixelSize;
        const ushort w = wnd_info_.size.x;
        const ushort h = wnd_info_.size.y;
        const std::size_t data_size = w * h * kPixelSize;

        int screen = ::XDefaultScreen(wnd_info_.display);
        int depth = ::XDefaultDepth(wnd_info_.display, screen);
        Visual* visual = ::XDefaultVisual(wnd_info_.display, screen);
        assert(depth <= pad);

        memory_image_data1_ = new std::uint8_t[data_size];
        memory_image1_ = ::XCreateImage(
            wnd_info_.display, visual, depth, ZPixmap, 0,
            reinterpret_cast<char*>(memory_image_data1_), w, h, pad, 0);
        assert(memory_image1_ != nullptr);

        memory_image_data2_ = new std::uint8_t[data_size];
        memory_image2_ = ::XCreateImage(
            wnd_info_.display, visual, depth, ZPixmap, 0,
            reinterpret_cast<char*>(memory_image_data2_), w, h, pad, 0);
        assert(memory_image2_ != nullptr);
    }
}

void PlatformWindow::DestroyMemoryImage()
{
    // The XDestroyImage function frees both the image structure and the data
    // pointed to by the image structure.

    if (memory_image1_ != nullptr)
    {
        XDestroyImage(memory_image1_);
        memory_image1_ = nullptr;
        memory_image_data1_ = nullptr;
    }

    if (memory_image2_ != nullptr)
    {
        XDestroyImage(memory_image2_);
        memory_image2_ = nullptr;
        memory_image_data2_ = nullptr;
    }
}

void PlatformWindow::WriteMemoryImageMT(const float* rgb, int first, int last)
{
    if (memory_image_data1_ != nullptr)
    {
        auto Saturate = [](float f) { return std::clamp(f, 0.0f, 1.0f); };
        const float* src = rgb + 3 * first;
        std::uint8_t* dest = memory_image_data1_ + kPixelSize * first;

        for (int n = last - first; n > 0; --n, src += 3, dest += kPixelSize)
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

        XGraphicsExposeEvent event = {};
        event.type = GraphicsExpose;
        event.send_event = True;
        event.display = wnd_info_.display;
        event.drawable = native_handle_;

         [[maybe_unused]] Status status = ::XSendEvent(wnd_info_.display,
            native_handle_, False, 0, reinterpret_cast<XEvent*>(&event));
        assert(status != 0);
        ::XFlush(wnd_info_.display);
    }
}

void PlatformWindow::PresentMemoryImage()
{
    std::lock_guard<std::mutex> locker(memory_image_mutex_);
   
    if (memory_image2_ != nullptr && memory_image_dirty_)
    {
        int screen = ::XDefaultScreen(wnd_info_.display);
        GC gc = ::XDefaultGC(wnd_info_.display, screen);
        ::XPutImage(wnd_info_.display, native_handle_, gc, memory_image2_,
            0, 0, 0, kWindowMenuHeight, wnd_info_.size.x, wnd_info_.size.y);
        ::XFlushGC(wnd_info_.display, gc);
        memory_image_dirty_ = false;
    }
}

template <typename EventType>
bool PlatformWindow::ForwardSubWindowEvent(const EventType& event)
{
    if (event.subwindow != None)
    {
        EventType sub_event = event;
        sub_event.window = event.subwindow;
        sub_event.subwindow = None;
        [[maybe_unused]] Bool success = ::XTranslateCoordinates(
            event.display, event.window, event.subwindow, event.x, event.y,
            &sub_event.x, &sub_event.y, &sub_event.subwindow);
        assert(success);
        menu_.ProcessEvent(reinterpret_cast<XEvent&>(sub_event));
        return true;
    }
    return false;
}

void PlatformWindow::OnEventEnterNotify()
{
    // pointer leaves menu when it enters window.
    XCrossingEvent event = {};
    event.type = LeaveNotify;
    event.send_event = True;
    event.display = wnd_info_.display;
    event.window = menu_.NativeHandle();
    menu_.ProcessEvent(reinterpret_cast<XEvent&>(event));
}

void PlatformWindow::OnEventMotionNotify(short x, short y)
{
    if (mouse_position_)
    {
        float delta_x = static_cast<float>(x - mouse_position_->x);
        float delta_y = static_cast<float>(y - mouse_position_->y);
        bool lbutton_pressed = pressed_buttons_.count(Button1);
        bool rbutton_pressed = pressed_buttons_.count(Button3);
        if (lbutton_pressed && OnMouseDragL)
            OnMouseDragL(delta_x, delta_y);
        else if (rbutton_pressed && OnMouseDragR)
            OnMouseDragR(delta_x, delta_y);
    }
    mouse_position_ = {x, y};
}

void PlatformWindow::OnEventButtonPress(unsigned int button)
{
    pressed_buttons_.insert(button);

    if (button == Button4)
    {
        if (OnMouseWheel)
            OnMouseWheel(1.0f);
    }
    else if (button == Button5)
    {
        if (OnMouseWheel)
            OnMouseWheel(-1.0f);
    }
}

void PlatformWindow::OnEventButtonRelease(unsigned int button)
{
    pressed_buttons_.erase(button);
}

bool PlatformWindow::HandleWindowClose()
{
    if (IsAlive() && closed_)
    {
        pressed_buttons_.clear();
        mouse_position_.reset();
        DestroyMemoryImage();
        menu_.Destroy();
        ::XDestroyWindow(wnd_info_.display, native_handle_);
        native_handle_ = None;
    }
    return !IsAlive();
}

//==============================================================================
// PlatformSystem
//==============================================================================

static int SelectFontAsDefault(const XFontStruct* font_infos, int font_count)
{
    constexpr int kFontSizeX[] = {8, 9, 10, 11};
    constexpr int kFontSizeY[] = {13, 14, 15, 16};
    std::pair<int, int> best_font = {0, INT_MAX};
    for (int i = 0; i < font_count; ++i)
    {
        const XFontStruct& font_info = font_infos[i];
        int w = font_info.max_bounds.rbearing - font_info.min_bounds.lbearing;
        int h = font_info.ascent + font_info.descent;
        int idx_w = std::find(kFontSizeX, std::end(kFontSizeX), w) - kFontSizeX;
        int idx_h = std::find(kFontSizeY, std::end(kFontSizeY), h) - kFontSizeY;
        int font_priority = (idx_w + 1) * (idx_h + 1);
        if (font_priority < best_font.second)
            best_font = {i, font_priority};
    }
    return best_font.first;
}

static Font SelectFontAsDefault(Display* display)
{
    constexpr const char* kFontPatterns[] = {
      "*courier-medium-r*", "*misc-fixed-medium-r*", "*medium-r*"};
    for (const char* font_pattern : kFontPatterns)
    {
        constexpr int maxnames = 1024;
        int font_count = 0;
        XFontStruct* font_infos = nullptr;
        if (char** font_names = ::XListFontsWithInfo(display,
            font_pattern, maxnames, &font_count, &font_infos))
        {
            int font_idx = SelectFontAsDefault(font_infos, font_count);
            const char* font_name = font_names[font_idx];
            Font font = ::XLoadFont(display, font_name);
            ::XFreeFontInfo(font_names, font_infos, font_count);
            return font;
        }
    }
    return None;
}

static XColor AllocDefaultColor(Display* display, const char* spec)
{
    int screen = ::XDefaultScreen(display);
    Colormap colormap = ::XDefaultColormap(display, screen);
    XColor color;
    ::XParseColor(display, colormap, spec, &color);
    ::XAllocColor(display, colormap, &color);
    return color;
}

//==============================================================================
// GenericWindow
//==============================================================================

static Display* display = nullptr;

static Font font = None;

static XColor silver;

void GenericWindow::InitializePlatform()
{
    ::XInitThreads();
    display = ::XOpenDisplay(nullptr);
    font = SelectFontAsDefault(display);
    silver = AllocDefaultColor(display, "#C0C0C0");
}

void GenericWindow::UnInitializePlatform()
{
    ::XUnloadFont(display, font);
    ::XCloseDisplay(display);
    font = None;
    display = nullptr;
}

std::filesystem::path GenericWindow::CurrentPath()
{
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::filesystem::path GenericWindow::ChooseModelFile()
{
    return std::filesystem::path();
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
    PlatformWindowInfo wnd_info;
    wnd_info.display = display;
    wnd_info.size = {static_cast<short>(width), static_cast<short>(height)};
    wnd_info.font = font;
    wnd_info.highlight_color = silver.pixel;
    impl_->Create(wnd_info);
    impl_->SetTitle(title);
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