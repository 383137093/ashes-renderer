#pragma once

#include <memory>
#include <filesystem>
#include <functional>
#include "GenericMenuParams.h"

namespace Ashes {

class GenericWindow
{
public:

    static void InitializePlatform();
    static void UnInitializePlatform();
    static std::filesystem::path CurrentPath();
    static std::filesystem::path ChooseModelFile();

    GenericWindow();
    GenericWindow(const GenericWindow&) = delete;
    ~GenericWindow();
    GenericWindow& operator = (const GenericWindow&) = delete;

    void Create(const char* title, int width, int height);
    void Close();
    void SetTitle(const char* title);
    void BuildMenu(std::shared_ptr<GenericMenuParams> menu_params);
    bool RunMessageLoop();

    void CreateMemoryImage();
    void WriteMemoryImageMT(const float* rgb, int first, int last);
    void SubmitMemoryImageMT();

    void SetOnWindowClose(std::function<void()> func);
    void SetOnMouseDragL(std::function<void(float, float)> func);
    void SetOnMouseDragR(std::function<void(float, float)> func);
    void SetOnMouseWheel(std::function<void(float)> func);

private:
    
    class PlatformWindow* impl_ = nullptr;
};

}