#include "GenericWindow.h"
#include <map>
#include <string>
#include <cstdint>
#include <algorithm>
#include <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>

namespace Ashes { class PlatformWindow; }

@interface PlatformWindowView : NSView
- (instancetype)initWithOwner:(Ashes::PlatformWindow*)owner;
@end

@interface PlatformMenu : NSObject<NSMenuDelegate>
- (bool)isAlive;
- (void)create:(std::shared_ptr<Ashes::GenericMenuParams>)menuParams;
- (void)destroy;
@end

namespace Ashes {

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
    void Create(int width, int height);
    void Close();
    void SetTitle(const char* title);
    void BuildMenu(std::shared_ptr<GenericMenuParams> menu_params);
    bool RunMessageLoop();

    NSBitmapImageRep* GetMemoryImageRep() const;
    void CreateMemoryImage();
    void DestroyMemoryImage();
    void WriteMemoryImageMT(const float* rgb, int first, int last);
    void SubmitMemoryImageMT();

    std::function<void()>             OnWindowClose;
    std::function<void(float, float)> OnMouseDragL;
    std::function<void(float, float)> OnMouseDragR;
    std::function<void(float)>        OnMouseWheel;

private:

    void WillCloseNotification();

    int                        width_ = 0;
    int                        height_ = 0;
    __strong NSWindow*         native_handle_ = nil;
    __strong id                will_close_observer_ = nil;
    __strong PlatformMenu*     menu_ = nil;
    __strong NSBitmapImageRep* memory_image_rep_ = nil;
    unsigned char*             memory_image_data_ = nullptr;
};

PlatformWindow::PlatformWindow()
{
    menu_ = [PlatformMenu new];
}

PlatformWindow::~PlatformWindow()
{
    assert(!IsAlive());
}

bool PlatformWindow::IsAlive() const
{
    bool alive = (native_handle_ != nil);
    if (!alive)
    {
        assert(![menu_ isAlive]);
        assert(will_close_observer_ == nil);
        assert(memory_image_rep_ == nil);
        assert(memory_image_data_ == nullptr);
    }
    return alive;
}

void PlatformWindow::Create(int width, int height)
{
    if (!IsAlive())
    {
        const NSWindowStyleMask style = (NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskTitled);
        NSRect content_rect = NSMakeRect(0, 0, width, height);
        auto* native_handle = [[NSWindow alloc]
            initWithContentRect:content_rect styleMask:style
            backing:NSBackingStoreBuffered defer:YES];
        assert(native_handle != nil);
        
        auto* content_view = [[PlatformWindowView alloc] initWithOwner:this];
        [native_handle setContentView:content_view];
        [native_handle setColorSpace:[NSColorSpace genericRGBColorSpace]];
        [native_handle makeKeyAndOrderFront:nil];
        [native_handle center];
        
        width_ = width;
        height_ = height;
        native_handle_ = native_handle;
        will_close_observer_ = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowWillCloseNotification
            object:native_handle queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification*) { WillCloseNotification(); }];
    }
}

void PlatformWindow::SetTitle(const char* title)
{
    if (IsAlive())
    {
        [native_handle_ setTitle:[NSString stringWithUTF8String:title]];
    }
}

void PlatformWindow::Close()
{
    if (IsAlive())
    {
        [native_handle_ performClose:0];
    }
}

void PlatformWindow::BuildMenu(std::shared_ptr<GenericMenuParams> menu_params)
{
    if (IsAlive())
    {
        [menu_ destroy];
        [menu_ create:menu_params];
    }
}

bool PlatformWindow::RunMessageLoop()
{
    NSEvent* event = [native_handle_
        nextEventMatchingMask:NSEventMaskAny
        untilDate:[NSDate distantFuture]
        inMode:NSDefaultRunLoopMode dequeue:YES];
    
    while (event != nil)
    {
        [NSApp sendEvent:event];
        event = [native_handle_
            nextEventMatchingMask:NSEventMaskAny
            untilDate:[NSDate distantPast]
            inMode:NSDefaultRunLoopMode dequeue:YES];
    }
    
    return IsAlive();
}

void PlatformWindow::WillCloseNotification()
{
    DestroyMemoryImage();
    [menu_ destroy];
    [[NSNotificationCenter defaultCenter] removeObserver:will_close_observer_];
    will_close_observer_ = nil;
    native_handle_ = nil;
    if (OnWindowClose) { OnWindowClose(); }
}

NSBitmapImageRep* PlatformWindow::GetMemoryImageRep() const
{
    return memory_image_rep_;
}

void PlatformWindow::CreateMemoryImage()
{
    if (IsAlive() && memory_image_rep_ == nil)
    {
        memory_image_data_ = new unsigned char[3 * width_ * height_];
        memory_image_rep_ = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:&memory_image_data_
            pixelsWide:width_ pixelsHigh:height_
            bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO isPlanar:NO
            colorSpaceName:NSCalibratedRGBColorSpace
            bytesPerRow:3*width_ bitsPerPixel:24];
    }
}

void PlatformWindow::DestroyMemoryImage()
{
    if (memory_image_rep_ != nil)
    {
        delete memory_image_data_;
        memory_image_rep_ = nil;
        memory_image_data_ = nullptr;
    }
}

void PlatformWindow::WriteMemoryImageMT(const float* rgb, int first, int last)
{
    if (memory_image_data_ != nullptr)
    {
        auto Saturate = [](float f) { return std::clamp(f, 0.0f, 1.0f); };
        const float* src = rgb + 3 * first;
        std::uint8_t* dest = memory_image_data_ + 3 * first;

        for (int n = last - first; n > 0; --n, src += 3, dest += 3)
        {
            dest[0] = static_cast<std::uint8_t>(Saturate(src[0]) * 255.0f);
            dest[1] = static_cast<std::uint8_t>(Saturate(src[1]) * 255.0f);
            dest[2] = static_cast<std::uint8_t>(Saturate(src[2]) * 255.0f);
        }
    }
}

void PlatformWindow::SubmitMemoryImageMT()
{
    dispatch_async(dispatch_get_main_queue(),
        ^{ [[native_handle_ contentView] setNeedsDisplay:YES]; });
}

//==============================================================================
// GenericWindow
//==============================================================================

void GenericWindow::InitializePlatform()
{
    if (NSApp == nil)
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
    }
}

void GenericWindow::UnInitializePlatform()
{
    [NSApp terminate:nil];
}

std::filesystem::path GenericWindow::CurrentPath()
{
    static const std::filesystem::path kCurrentPath = []() {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, 0);
        [[maybe_unused]] int ret = _NSGetExecutablePath(path.data(), &size);
        assert(ret == 0 && size == path.size());
        path.erase(path.rfind("/AshesRenderer.app"));
        return path;
    }();
    return kCurrentPath;
}

std::filesystem::path GenericWindow::ChooseModelFile()
{
    NSOpenPanel* open_panel = [NSOpenPanel openPanel];
    [open_panel setCanChooseFiles:YES];
    [open_panel setCanChooseDirectories:NO];
    [open_panel setAllowsMultipleSelection:NO];
    [open_panel setResolvesAliases:YES];
    [open_panel setAllowedFileTypes:@[@"obj", @"gltf"]];
    [open_panel setTitle:@"SelectModelFile"];
    [open_panel setMessage:@"Select a Model File"];
    [open_panel setPrompt:@"Select"];
    [open_panel setDirectoryURL:[NSURL fileURLWithFileSystemRepresentation:
        CurrentPath().string().data() isDirectory:YES relativeToURL:nil]];
    NSInteger result = [open_panel runModal];
    if (result == NSModalResponseOK)
        return [[open_panel URL] fileSystemRepresentation];
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
    impl_->Create(width, height);
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

//==============================================================================
// PlatformWindowView
//==============================================================================

@implementation PlatformWindowView {
    Ashes::PlatformWindow* _owner;
}

- (instancetype)initWithOwner:(Ashes::PlatformWindow*)owner {
    self = [super init];
    if (self) {
        _owner = owner;
    }
    return self;
}

- (void)mouseDragged:(NSEvent*)event {
    if (_owner->OnMouseDragL) {
        _owner->OnMouseDragL([event deltaX], [event deltaY]);
    }
}

- (void)rightMouseDragged:(NSEvent*)event {
    if (_owner->OnMouseDragR) {
        _owner->OnMouseDragR([event deltaX], [event deltaY]);
    }
}

- (void)scrollWheel:(NSEvent*)event {
    if (_owner->OnMouseWheel) {
        _owner->OnMouseWheel([event deltaY] * 0.1f);
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    if (NSBitmapImageRep* rep = _owner->GetMemoryImageRep()) {
        if (rep.bitmapData != nil) {
            [rep drawInRect:dirtyRect];
        }
    }
}

@end

//==============================================================================
// PlatformMenu
//==============================================================================

@implementation PlatformMenu {
    std::shared_ptr<Ashes::GenericMenuParams>    _menuParams;
    NSMenu*                                      _nativeHandle;
    std::map<NSMenuItem*, std::function<void()>> _onItemSelected;
    NSMutableArray<PlatformMenu*>*               _subMenus;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _subMenus = [NSMutableArray<PlatformMenu*> new];
    }
    return self;
}

- (NSMenu*)nativeHandle {
    return _nativeHandle;
}

- (bool)isAlive {
    bool alive = (_nativeHandle != nil);
    if (!alive) {
        assert(_onItemSelected.empty());
        assert(_subMenus.count == 0);
    }
    return alive;
}

- (void)create:(std::shared_ptr<Ashes::GenericMenuParams>)menuParams {
    if (![self isAlive]) {
        _menuParams = nullptr;
        [self build:menuParams];
        [NSApp setMainMenu:_nativeHandle];
    }
}

- (void)destroy {
    if ([self isAlive]) {
        [_subMenus removeAllObjects];
        _nativeHandle = nil;
        _onItemSelected.clear();
    }
}

- (void)build:(std::shared_ptr<Ashes::GenericMenuParams>)menuParams {
    assert(_menuParams == nullptr);
    assert(![self isAlive]);
    using namespace Ashes;
    _menuParams = menuParams;
    _nativeHandle = [NSMenu new];
    _nativeHandle.delegate = self;
    for (const GenericMenuItemParamsVariant& item_params : _menuParams->items) {
        if (auto* btn = item_params.As<GenericMenuButtonParams>()) {
            [self addItem:btn->text action:btn->on_clicked];
        } else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>()) {
            for (int i = 0; i < static_cast<int>(btns->texts.size()); ++i) {
                std::function<void()> action = [btns, i]() {
                    btns->on_clicked(i == btns->get_selection() ? -1 : i); };
                [self addItem:btns->texts[i] action:std::move(action)];
            }
        } else if (auto* pop_btn = item_params.As<GenericMenuPopupButtonParams>()) {
            [self addSubMenu:pop_btn->text menuParams:pop_btn->menu_params];
        }
    }
}

- (void)addItem:(const std::string&)text action:(std::function<void()>)action {
    if (text != Ashes::kGenericMenuSeparatorText) {
        NSMenuItem* item = [NSMenuItem new];
        item.title = [NSString stringWithUTF8String:text.data()];
        [item setTarget:self];
        [item setAction:@selector(itemSelected:)];
        _onItemSelected.emplace(item, std::move(action));
        [_nativeHandle addItem:item];
    } else {
        [_nativeHandle addItem:[NSMenuItem separatorItem]];
    }
}

- (void)addSubMenu:(const std::string&)text
        menuParams:(std::shared_ptr<Ashes::GenericMenuParams>)menuParams {
    PlatformMenu* subMenu = [PlatformMenu new];
    [subMenu build:menuParams];
    [_subMenus addObject:subMenu];
    NSMenuItem* item = [NSMenuItem new];
    item.title = [NSString stringWithUTF8String:text.data()];
    [item setSubmenu:[subMenu nativeHandle]];
    [_nativeHandle addItem:item];
}

- (void)menuWillOpen:(NSMenu*)menu {
    assert(menu == _nativeHandle);
    using namespace Ashes;
    auto enumerator = [_nativeHandle.itemArray objectEnumerator];
    for (const GenericMenuItemParamsVariant& item_params : _menuParams->items) {
        if (auto* btn = item_params.As<GenericMenuButtonParams>()) {
            [enumerator nextObject].state = (btn->is_checked()
                ? NSControlStateValueOn : NSControlStateValueOff);
        } else if (auto* btns = item_params.As<GenericMenuButtonGroupParams>()) {
            const int selection = btns->get_selection();
            for (int i = 0; i < static_cast<int>(btns->texts.size()); ++i) {
                [enumerator nextObject].state = (i == selection
                    ? NSControlStateValueOn : NSControlStateValueOff);
            }
        } else if (item_params.As<GenericMenuPopupButtonParams>()) {
            [enumerator nextObject].state = NSControlStateValueOff;
        }
    }
}

- (void)itemSelected:(NSMenuItem*)sender {
    auto iter = _onItemSelected.find(sender);
    if (iter != _onItemSelected.end()) { iter->second(); }
}

@end
