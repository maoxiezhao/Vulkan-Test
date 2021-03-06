#include "settings.h"
#include "editor.h"
#include "core\filesystem\filesystem.h"
#include "core\scripts\luaUtils.h"
#include "core\scripts\luaType.h"
#include "imgui-docking\imgui.h"

namespace VulkanTest
{
namespace LuaUtils
{
	template<>
	struct LuaTypeNormalMapping<ImVec2>
	{
		static ImVec2 Get(lua_State* l, int index)
		{
			F32x2 ret = LuaType<F32x2>::Get(l, index);
			return ImVec2(ret.x, ret.y);
		}

		static ImVec2 Opt(lua_State* l, int index, const ImVec2& defValue)
		{
			return LuaType<F32x2>::Check(l, index) ? Get(l, index) : defValue;
		}
	};
}

namespace Editor
{
	// TODO: use ScriptConfig to replace hands-write codes
	static const char SETTINGS_PATH[] = "editor.ini";

	Settings::Settings(EditorApp& app_) :
		app(app_)
	{
		window.x = window.y = 0;
		window.w = window.h = -1;

		luaState = luaL_newstate();
		luaL_openlibs(luaState);
		lua_newtable(luaState);
		lua_setglobal(luaState, "custom");
	}

	Settings::~Settings()
	{
		lua_close(luaState);
	}

	template<typename T>
	static int GetFromGlobal(lua_State* l, const char* name, T defaultValue)
	{
		T value = defaultValue;
		lua_getglobal(l, name);
		value = LuaUtils::Opt<T>(l, -1, defaultValue);
		lua_pop(l, 1);
		return value;
	}

	void LoadEditorStyle(lua_State* l)
	{
		lua_getglobal(l, "style");
		if (lua_type(l, -1) != LUA_TTABLE)
		{
			lua_pop(l, 1);
			return;
		}

		auto& style = ImGui::GetStyle();
#define LOAD_FLOAT(name) style.name = LuaUtils::OptField<F32>(l, #name, style.name);
#define LOAD_BOOL(name) style.name = LuaUtils::OptField<bool>(l, #name, style.name);
#define LOAD_VEC2(name) style.name = LuaUtils::OptField<ImVec2>(l, #name, style.name);

		LOAD_VEC2(FramePadding);
		LOAD_FLOAT(FrameRounding);
		LOAD_FLOAT(FrameBorderSize);
		LOAD_VEC2(ItemSpacing);
		LOAD_VEC2(ItemInnerSpacing);
		LOAD_VEC2(TouchExtraPadding);

#undef LOAD_FLOAT
#undef LOAD_BOOL
#undef LOAD_VEC2
		lua_pop(l, 1);
	}

	void SaveEditorStyle(File& file)
	{
		auto& style = ImGui::GetStyle();
		file << "style = {";

#define SAVE_FLOAT(name) do { file << "\t" #name " = " << style.name << ",\n"; } while(false)
#define SAVE_BOOL(name) do { file << "\t" #name " = " << (style.name ? "true" : "false") << ",\n"; } while(false)
#define SAVE_VEC2(name) do { file << "\t" #name " = {" << style.name.x << ", " << style.name.y << "},\n"; } while(false)

		SAVE_VEC2(FramePadding);
		SAVE_FLOAT(FrameRounding);
		SAVE_FLOAT(FrameBorderSize);
		SAVE_VEC2(ItemSpacing);
		SAVE_VEC2(ItemInnerSpacing);
		SAVE_VEC2(TouchExtraPadding);

#undef SAVE_BOOL
#undef SAVE_FLOAT
#undef SAVE_VEC2

		file << "}\n";
	}

	bool Settings::Load()
	{
		lua_State* l = luaState;
		FileSystem& fs = app.GetEngine().GetFileSystem();
		if (!fs.FileExists(SETTINGS_PATH))
		{
			Logger::Error("Failed to open %s", SETTINGS_PATH);
			return false;
		}

		OutputMemoryStream buf;
		if (!fs.LoadContext(SETTINGS_PATH, buf))
		{
			Logger::Error("Failed to open %s", SETTINGS_PATH);
			return false;
		}

		Span<const char> content((const char*)buf.Data(), (U32)buf.Size());
		if (!LuaUtils::Execute(l, content, "settings", 0))
			return false;
		
		// Check the version
		bool validVersion = false;
		lua_getglobal(l, "version");
		if (LuaUtils::Get<int>(l, -1) == 1)
			validVersion = true;
		lua_pop(l, 1);

		if (validVersion == false)
		{
			Logger::Error("Failed to open %s", SETTINGS_PATH);
			return false;
		}

		// Load window settings
		lua_getglobal(l, "window");
		if (lua_type(l, -1) == LUA_TTABLE)
		{
			window.x = LuaUtils::GetField<I32>(l, "x");
			window.y = LuaUtils::GetField<I32>(l, "y");
			window.w = LuaUtils::GetField<I32>(l, "w");
			window.h = LuaUtils::GetField<I32>(l, "h");
		}
		lua_pop(l, 1);

		// Load base properties
		fontSize = GetFromGlobal<I32>(l, "font_size", 13);

		// Load editor style
		LoadEditorStyle(l);

		// Load imgui settings
		lua_getglobal(l, "imgui");
		if (lua_type(l, -1) == LUA_TSTRING) {
			imguiState = lua_tostring(l, -1);
		}
		lua_pop(l, 1);

		// Load actions
		auto& actions = app.GetActions();
		lua_getglobal(l, "actions");
		if (lua_type(l, -1) == LUA_TTABLE)
		{
			for (auto action : actions)
			{
				lua_getfield(l, -1, action->name);
				if (lua_type(l, -1) == LUA_TTABLE)
				{
					lua_getfield(l, -1, "shortcut");
					if (lua_isnumber(l, -1))
						action->shortcut = (Platform::Keycode)lua_tonumber(l, -1);
					lua_pop(l, 1);
				}
				lua_pop(l, 1);
			}
		}
		lua_pop(l, 1);

		return true;
	}

	static void WriteCustom(lua_State* L, struct IOutputStream& file)
	{
		file << "custom = {\n";
		lua_getglobal(L, "custom");
		lua_pushnil(L);
		bool first = true;
		while (lua_next(L, -2))
		{
			if (!first) file << ",\n";
			const char* name = lua_tostring(L, -2);
			switch (lua_type(L, -1))
			{
			case LUA_TBOOLEAN:
				file << name << " = " << (lua_toboolean(L, -1) != 0 ? "true" : "false");
				break;
			case LUA_TNUMBER:
				file << name << " = " << lua_tonumber(L, -1);
				break;
			default:
				ASSERT(false);
				break;
			}
			lua_pop(L, 1);
			first = false;
		}
		lua_pop(L, 1);
		file << "}\n";
	}

	bool Settings::Save()
	{
		FileSystem& fs = app.GetEngine().GetFileSystem();
		auto file = fs.OpenFile(SETTINGS_PATH, FileFlags::DEFAULT_WRITE);
		if (!file->IsValid())
		{
			Logger::Error("Failed to save the settings");
			return false;
		}

		// Version
		file->Write("version = 1\n");

		*file << "window = { x = " 
			<< window.x
			<< ", y = " << window.y
			<< ", w = " << window.w
			<< ", h = " << window.h << " }\n";

		*file << "font_size = " << fontSize << "\n";

		// Write custom values
		WriteCustom(luaState, *file);

		// Editor styles
		SaveEditorStyle(*file);

		// Imgui state
		file->Write("imgui = [[");
		file->Write(imguiState.c_str());
		file->Write("]]\n");

		// Actions
		auto& actions = app.GetActions();
		*file << "actions = {\n";
		for (auto action : actions)
		{
			*file << "\t" << action->name << " = { shortcut = "
				<< (int)action->shortcut << "},\n";
		}
		*file << "}\n";

		file->Close();
		return true;
	}

	void Settings::OnGUI()
	{
		if (!isOpen) return;

		if (ImGui::Begin(ICON_FA_COG "Settings##settings", &isOpen))
		{
			if (ImGui::BeginTabBar("tabs"))
			{
				for (auto plugin : plugins)
				{
					if (ImGui::BeginTabItem(plugin->GetName()))
					{
						plugin->OnGui(*this);
						ImGui::EndTabItem();
					}
				}

				if (ImGui::BeginTabItem("Style"))
				{
					ImGui::InputInt("Font size (needs restart)", &fontSize);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void Settings::AddPlugin(IPlugin* plugin)
	{
		for (auto& p : plugins)
		{
			if (p == plugin)
				return;
		}
		plugins.push_back(plugin);
	}

	void Settings::RemovePlugin(IPlugin* plugin)
	{
		plugins.erase(plugin);
	}

	void Settings::SetValue(const char* name, bool value) const
	{
		lua_getglobal(luaState, "custom");
		lua_pushboolean(luaState, value);
		lua_setfield(luaState, -2, name);
		lua_pop(luaState, 1);
	}

	void Settings::SetValue(const char* name, float value) const
	{
		lua_getglobal(luaState, "custom");
		lua_pushnumber(luaState, value);
		lua_setfield(luaState, -2, name);
		lua_pop(luaState, 1);
	}

	void Settings::SetValue(const char* name, int value) const
	{
		lua_getglobal(luaState, "custom");
		lua_pushinteger(luaState, value);
		lua_setfield(luaState, -2, name);
		lua_pop(luaState, 1);
	}

	float Settings::GetValue(const char* name, float default_value) const
	{
		lua_getglobal(luaState, "custom");
		F32 ret = LuaUtils::OptField(luaState, name, default_value);
		lua_pop(luaState, 1);
		return ret;
	}

	int Settings::GetValue(const char* name, int default_value) const
	{
		lua_getglobal(luaState, "custom");
		int ret = LuaUtils::OptField(luaState, name, default_value);
		lua_pop(luaState, 1);
		return ret;
	}

	bool Settings::GetValue(const char* name, bool default_value) const
	{
		lua_getglobal(luaState, "custom");
		bool ret = LuaUtils::OptField(luaState, name, default_value);
		lua_pop(luaState, 1);
		return ret;
	}
}
}