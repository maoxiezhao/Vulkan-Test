#include "editor.h"
#include "editorUtils.h"
#include "core\utils\profiler.h"
#include "core\platform\platform.h"
#include "renderer\renderer.h"
#include "renderer\renderPath3D.h"
#include "editor\renderer\imguiRenderer.h"

#include "widgets\assetBrowser.h"
#include "widgets\assetCompiler.h"
#include "widgets\worldEditor.h"

#include "plugins\renderer.h"

#include "imgui-docking\imgui.h"
#include "renderer\imguiRenderer.h"

namespace VulkanTest
{
namespace Editor
{
    class EditorAppImpl final : public EditorApp
    {
    public:
        EditorAppImpl()
        {
            memset(imguiKeyMap, 0, sizeof(imguiKeyMap));
            imguiKeyMap[(int)Platform::Keycode::CTRL] = ImGuiKey_ModCtrl;
            imguiKeyMap[(int)Platform::Keycode::MENU] = ImGuiKey_ModAlt;
            imguiKeyMap[(int)Platform::Keycode::SHIFT] = ImGuiKey_ModShift;
            imguiKeyMap[(int)Platform::Keycode::LSHIFT] = ImGuiKey_LeftShift;
            imguiKeyMap[(int)Platform::Keycode::RSHIFT] = ImGuiKey_RightShift;
            imguiKeyMap[(int)Platform::Keycode::SPACE] = ImGuiKey_Space;
            imguiKeyMap[(int)Platform::Keycode::TAB] = ImGuiKey_Tab;
            imguiKeyMap[(int)Platform::Keycode::LEFT] = ImGuiKey_LeftArrow;
            imguiKeyMap[(int)Platform::Keycode::RIGHT] = ImGuiKey_RightArrow;
            imguiKeyMap[(int)Platform::Keycode::UP] = ImGuiKey_UpArrow;
            imguiKeyMap[(int)Platform::Keycode::DOWN] = ImGuiKey_DownArrow;
            imguiKeyMap[(int)Platform::Keycode::PAGEUP] = ImGuiKey_PageUp;
            imguiKeyMap[(int)Platform::Keycode::PAGEDOWN] = ImGuiKey_PageDown;
            imguiKeyMap[(int)Platform::Keycode::HOME] = ImGuiKey_Home;
            imguiKeyMap[(int)Platform::Keycode::END] = ImGuiKey_End;
            imguiKeyMap[(int)Platform::Keycode::DEL] = ImGuiKey_Delete;
            imguiKeyMap[(int)Platform::Keycode::BACKSPACE] = ImGuiKey_Backspace;
            imguiKeyMap[(int)Platform::Keycode::RETURN] = ImGuiKey_Enter;
            imguiKeyMap[(int)Platform::Keycode::ESCAPE] = ImGuiKey_Escape;
            imguiKeyMap[(int)Platform::Keycode::A] = ImGuiKey_A;
            imguiKeyMap[(int)Platform::Keycode::C] = ImGuiKey_C;
            imguiKeyMap[(int)Platform::Keycode::V] = ImGuiKey_V;
            imguiKeyMap[(int)Platform::Keycode::X] = ImGuiKey_X;
            imguiKeyMap[(int)Platform::Keycode::Y] = ImGuiKey_Y;
            imguiKeyMap[(int)Platform::Keycode::Z] = ImGuiKey_Z;
        }

        ~EditorAppImpl()
        {
        }

        void Initialize() override
        {
            // Init platform
            bool ret = platform->Init(GetDefaultWidth(), GetDefaultHeight(), GetWindowTitle());
            ASSERT(ret);

            // Init wsi
            ret = wsi.Initialize(Platform::GetCPUsCount() + 1);
            ASSERT(ret);

            // Add main window
            windows.push_back(platform->GetWindow());

            // Create game engine
            Engine::InitConfig config = {};
            config.windowTitle = GetWindowTitle();
            engine = CreateEngine(config, *this);

            assetCompiler = AssetCompiler::Create(*this);
            worldEditor = WorldEditor::Create(*this);

            // Get renderer
            renderer = static_cast<RendererPlugin*>(engine->GetPluginManager().GetPlugin("Renderer"));
            ASSERT(renderer != nullptr);

            // Init imgui renderer
            ImGuiRenderer::Initialize(*this);

            // Init actions
            InitActions();

            // Load plugins
            LoadPlugins();

            assetCompiler->InitFinished();
        }

        void Uninitialize() override
        {
            FileSystem& fs = engine->GetFileSystem();
            while (fs.HasWork())
                fs.ProcessAsync();

            // Destroy world
            worldEditor->DestroyWorld();

            // Clear widgets
            for (EditorWidget* widget : widgets)
                CJING_SAFE_DELETE(widget);
            widgets.clear();

            // Clear editor plugins
            for (EditorPlugin* plugin : plugins)
                CJING_SAFE_DELETE(plugin);
            plugins.clear();

            // Remove actions
            for (Utils::Action* action : actions)
                CJING_SAFE_DELETE(action);
            actions.clear();

            // Remove system widgets
            assetCompiler.Reset();
            worldEditor.Reset();

            // Uninit imgui renderer
            ImGuiRenderer::Uninitialize();

            // Reset engine
            engine.Reset();

            // Uninit platform
            wsi.Uninitialize();
            platform.reset();
        }

        void OnEvent(const Platform::WindowEvent& ent) override
        {
            bool isFocused = IsFocused();
            Platform::WindowType mainWindow = platform->GetWindow();
            switch (ent.type)
            {
            case Platform::WindowEvent::Type::MOUSE_MOVE: {
                ImGuiIO& io = ImGui::GetIO();
                const Platform::WindowPoint cp = Platform::GetMouseScreenPos();
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    io.AddMousePosEvent((float)cp.x, (float)cp.y);
                }
                else {
                    const Platform::WindowRect screenRect = Platform::GetWindowScreenRect(mainWindow);
                    io.AddMousePosEvent((float)cp.x - screenRect.left, (float)cp.y - screenRect.top);
                }
                break;
            }
            case Platform::WindowEvent::Type::FOCUS: {
                ImGuiIO& io = ImGui::GetIO();
                io.AddFocusEvent(IsFocused());
                break;
            }
            case Platform::WindowEvent::Type::MOUSE_BUTTON: {
                ImGuiIO& io = ImGui::GetIO();
                if (isFocused || !ent.mouseButton.down)
                    io.AddMouseButtonEvent((int)ent.mouseButton.button, ent.mouseButton.down);
                break;
            }
            case Platform::WindowEvent::Type::MOUSE_WHEEL:
                if (isFocused)
                {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddMouseWheelEvent(0, ent.mouseWheel.amount);
                }
                break;

            case Platform::WindowEvent::Type::CHAR:
                if (isFocused)
                {
                    ImGuiIO& io = ImGui::GetIO();
                    char tmp[5] = {};
                    memcpy(tmp, &ent.textInput.utf8, sizeof(ent.textInput.utf8));
                    io.AddInputCharactersUTF8(tmp);
                }
                break;

            case Platform::WindowEvent::Type::KEY:
                if (isFocused || !ent.key.down)
                {
                    ImGuiIO& io = ImGui::GetIO();
                    ImGuiKey key = imguiKeyMap[(int)ent.key.keycode];
                    if (key != ImGuiKey_None)
                        io.AddKeyEvent(key, ent.key.down);
                }
                break;

            case Platform::WindowEvent::Type::QUIT:
                RequestShutdown();
                break;

            case Platform::WindowEvent::Type::WINDOW_CLOSE: {
                ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(ent.window);
                if (vp)
                    vp->PlatformRequestClose = true;
                if (ent.window == mainWindow)
                    RequestShutdown();
                break;
            }
            case Platform::WindowEvent::Type::WINDOW_MOVE: {
                if (ImGui::GetCurrentContext())
                {
                    ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(ent.window);
                    if (vp)
                        vp->PlatformRequestMove = true;
                }
                break;
            }
            case Platform::WindowEvent::Type::WINDOW_SIZE:
                if (ImGui::GetCurrentContext()) 
                {
                    ImGuiViewport* vp = ImGui::FindViewportByPlatformHandle(ent.window);
                    if (vp)
                        vp->PlatformRequestResize = true;
                }

                if (ent.window == mainWindow && ent.winSize.w > 0 && ent.winSize.h > 0)
                    platform->NotifyResize(ent.winSize.w, ent.winSize.h);

                break;

            default:
                break;
            }
        }

        AssetCompiler& GetAssetCompiler()
        {
            return *assetCompiler;
        }

    protected:
        void Update(F32 deltaTime) override
        {
            PROFILE_BLOCK("Update");
            ProcessDeferredDestroyWindows();
           
            // Begin imgui frame
            ImGuiRenderer::BeginFrame();

            assetCompiler->Update(deltaTime);

            engine->Update(*worldEditor->GetWorld(), deltaTime);

            fpsFrame++;
            if (fpsTimer.GetTimeSinceTick() > 1.0f)
            {
                fps = fpsFrame / fpsTimer.Tick();
                fpsFrame = 0;
            }

            // Update editor widgets
            for (auto widget : widgets)
                widget->Update(deltaTime);

            OnGUI();

            // End imgui frame
            ImGuiRenderer::EndFrame();
        }

        void Render() override
        {
            wsi.BeginFrame();
            wsi.Begin();
            ImGuiRenderer::Render();
            wsi.End();
            wsi.EndFrame();
        }

    public:
        void AddPlugin(EditorPlugin& plugin) override
        {
            plugins.push_back(&plugin);
        }

        void AddWidget(EditorWidget& widget) override
        {
            widgets.push_back(&widget);
        }

        void RemoveWidget(EditorWidget& widget) override
        {
            widgets.swapAndPopItem(&widget);
        }

        void AddWindow(Platform::WindowType window) override
        {
            windows.push_back(window);
        }
        void RemoveWindow(Platform::WindowType window) override
        {
            windows.erase(window);
        }

        void DeferredDestroyWindow(Platform::WindowType window)override
        {
            toDestroyWindows.push_back({ window, 4 });
        }

        bool showDemoWindow = true;
        void OnGUI()
        {
            ImGuiWindowFlags flags = 
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoDocking;

            const bool hasviewports = ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable;

            Platform::WindowType window = platform->GetWindow();
            Platform::WindowRect rect = Platform::GetClientBounds(window);
            Platform::WindowPoint point = hasviewports ? Platform::ToScreen(window, rect.left, rect.top) : Platform::WindowPoint();
            if (rect.width > 0 && rect.height > 0)
            {
                ImGui::SetNextWindowSize(ImVec2((float)rect.width, (float)rect.height));
                ImGui::SetNextWindowPos(ImVec2((float)point.x, (float)point.y));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::Begin("MainDockspace", nullptr, flags);
                ImGui::PopStyleVar();

                // Show main menu
                OnMainMenu();
                ImGuiID dockspaceID = ImGui::GetID("MyDockspace");
                ImGui::DockSpace(dockspaceID, ImVec2(0, 0));
                ImGui::End();

                // Show editor widgets
                for (auto widget : widgets)
                    widget->OnGUI();

                if (showDemoWindow)
                    ImGui::ShowDemoWindow(&showDemoWindow);
            }
        }

        void OnMainMenu()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
            if (ImGui::BeginMainMenuBar())
            {
                OnFileMenu();
                ImGui::PopStyleVar(2);

                StaticString<128> fpsTxt("");
                fpsTxt << "FPS: ";
                fpsTxt << (U32)(fps + 0.5f);
                auto stats_size = ImGui::CalcTextSize(fpsTxt);
                ImGui::SameLine(ImGui::GetContentRegionMax().x - stats_size.x);
                ImGui::Text("%s", (const char*)fpsTxt);

                ImGui::EndMainMenuBar();
            }
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2));
        }

        void OnFileMenu()
        {
            if (!ImGui::BeginMenu("File")) return;

            OnActionMenuItem("Exit");
            ImGui::EndMenu();
        }

        void OnActionMenuItem(const char* name)
        {
            Utils::Action* action = GetAction(name);
            if (!action)
                return;

            if (ImGui::MenuItem(action->label, nullptr, action->isSelected.Invoke()))
                action->func.Invoke();
        }

    private:
        bool IsFocused() const 
        {
            const Platform::WindowType focused = Platform::GetActiveWindow();
            const int idx = windows.find([focused](Platform::WindowType w) { return w == focused; });
            return idx >= 0;
        }

        void ProcessDeferredDestroyWindows()
        {
            for (int i = toDestroyWindows.size() - 1; i >= 0; i--)
            {
                WindowToDestroy& toDestroy = toDestroyWindows[i];
                toDestroy.counter--;
                if (toDestroy.counter == 0)
                {
                    Platform::DestroyCustomWindow(toDestroy.window);
                    toDestroyWindows.swapAndPop(i);
                }
            }
        }

        void InitActions()
        {
            // File menu item actions
            AddAction<&EditorAppImpl::Exit>("Exit");
        }

        void LoadPlugins()
        {
            // TODO:
            {
                EditorPlugin* plugin = SetupPluginRenderer(*this);
                if (plugin != nullptr)
                    AddPlugin(*plugin);
            }

            // Init plugins
            for (EditorPlugin* plugin : plugins)
                plugin->Initialize();
        }

        template<void (EditorAppImpl::*Func)()>
        Utils::Action& AddAction(const char* label)
        {
            Utils::Action* action = CJING_NEW(Utils::Action)(label, label);
            action->func.Bind<Func>(this);
            actions.push_back(action);
            return *action;
        }

        Utils::Action* GetAction(const char* name)
        {
            for (Utils::Action* action : actions)
            {
                if (EqualString(action->name, name))
                    return action;
            }
            return nullptr;
        }

        void Exit()
        {
            RequestShutdown();
        }

    private:
        Array<EditorPlugin*> plugins;
        Array<EditorWidget*> widgets;
        F32 fps = 0.0f;
        U32 fpsFrame = 0;
        Timer fpsTimer;
        Array<Utils::Action*> actions;

        // Windows
        Array<Platform::WindowType> windows;
        struct WindowToDestroy 
        {
            Platform::WindowType window;
            U32 counter = 0;
        };
        Array<WindowToDestroy> toDestroyWindows;

        // Builtin widgets
        UniquePtr<AssetCompiler> assetCompiler;
        UniquePtr<WorldEditor> worldEditor;

        // Imgui
        ImGuiKey imguiKeyMap[255];
    };

    EditorApp* EditorApp::Create()
    {
		return CJING_NEW(EditorAppImpl)();
    }
    
    void EditorApp::Destroy(EditorApp* app)
    {
        // CJING_SAFE_DELETE(app);
    }
}
}