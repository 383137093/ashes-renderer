#include <filesystem>
#include "Platform/GenericWindow.h"
#include "Rasterize/RenderApp.h"

#ifdef WIN32
#include <Windows.h>
int wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
#else
int main()
#endif
{
    Ashes::GenericWindow::InitializePlatform();
    {
        std::filesystem::path filename = Ashes::GenericWindow::CurrentPath();
        filename /= "Resources/Models/gltf/pbr_material_reference/scene.gltf";
        Ashes::Rasterize::RenderApp render_app;
        render_app.Run(800, 600, filename);
    }
    Ashes::GenericWindow::UnInitializePlatform();
    return 0;
}