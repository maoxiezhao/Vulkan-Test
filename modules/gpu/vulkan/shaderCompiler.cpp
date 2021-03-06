#include "shaderCompiler.h"
#include "core\utils\helper.h"
#include "core\utils\archive.h"

#ifdef CJING3D_PLATFORM_WIN32
#include <SDKDDKVer.h>
#include <windows.h>
#include <tchar.h>
#include <atlbase.h> // ComPtr

#define CiLoadLibrary(name) LoadLibrary(_T(name))
#define CiGetProcAddress(handle,name) GetProcAddress(handle, name)

#define VK_USE_PLATFORM_WIN32_KHR
#endif

// dxcompiler
#include "dxcompiler\inc\d3d12shader.h"
#include "dxcompiler\inc\dxcapi.h"

namespace VulkanTest
{
namespace GPU
{
namespace ShaderCompiler
{
	struct CompilerImpl
	{
		DxcCreateInstanceProc DxcCreateInstance = nullptr;

		CompilerImpl()
		{
#ifdef _WIN32
#define LIBDXCOMPILER "dxcompiler.dll"
			HMODULE dxcompiler = CiLoadLibrary(LIBDXCOMPILER);
#elif defined(PLATFORM_LINUX)
#define LIBDXCOMPILER "libdxcompiler.so"
			HMODULE dxcompiler = CiLoadLibrary("./" LIBDXCOMPILER);
#endif
			if (dxcompiler != nullptr)
			{
				DxcCreateInstance = (DxcCreateInstanceProc)CiGetProcAddress(dxcompiler, "DxcCreateInstance");
				if (DxcCreateInstance != nullptr)
				{
					CComPtr<IDxcCompiler3> dxcCompiler;
					HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
					ASSERT(SUCCEEDED(hr));
					CComPtr<IDxcVersionInfo> info;
					hr = dxcCompiler->QueryInterface(&info);
					ASSERT(SUCCEEDED(hr));
					uint32_t minor = 0;
					uint32_t major = 0;
					hr = info->GetVersion(&major, &minor);
					ASSERT(SUCCEEDED(hr));
					Logger::Info("ShaderCompiler loaded (version:%u.$u)", std::to_string(major), std::to_string(minor));
				}
			}
			else
			{
				Logger::Warning("ShaderCompiler loaded failed");
			}
		}
	};

	CompilerImpl& GetCompilerImpl()
	{
		static CompilerImpl impl;
		return impl;
	}

	struct IncludeHandler : public IDxcIncludeHandler
	{
		const CompilerInput* input = nullptr;
		CompilerOutput* output = nullptr;
		CComPtr<IDxcIncludeHandler> dxcIncludeHandler;

		HRESULT STDMETHODCALLTYPE LoadSource(
			_In_z_ LPCWSTR pFilename,                                 // Candidate filename.
			_COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource  // Resultant source object for included file, nullptr if not found.
		) override
		{
			HRESULT hr = dxcIncludeHandler->LoadSource(pFilename, ppIncludeSource);
			if (SUCCEEDED(hr))
			{
				std::string& filename = output->dependencies.emplace_back();
				Helper::StringConvert(pFilename, filename);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
		{
			return dxcIncludeHandler->QueryInterface(riid, ppvObject);
		}

		ULONG STDMETHODCALLTYPE AddRef(void) override
		{
			return 0;
		}
		ULONG STDMETHODCALLTYPE Release(void) override
		{
			return 0;
		}
	};
    
	bool Preprocess()
	{
		return true;
	}

	bool Compile(const CompilerInput& input, CompilerOutput& output)
	{
		CompilerImpl& impl = GetCompilerImpl();
		if (impl.DxcCreateInstance == nullptr)
			return false;

		CComPtr<IDxcUtils> dxcUtils;
		CComPtr<IDxcCompiler3> dxcCompiler;
		HRESULT hr = impl.DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
		assert(SUCCEEDED(hr));
		hr = impl.DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
		assert(SUCCEEDED(hr));

		std::vector<uint8_t> shadersourcedata;
		if (!Helper::FileRead(input.shadersourcefilename, shadersourcedata))
			return false;

		// https://github.com/microsoft/DirectXShaderCompiler/wiki/Using-dxc.exe-and-dxcompiler.dll#dxcompiler-dll-interface
		
		std::vector<LPCWSTR> args;
		args.push_back(L"-D"); args.push_back(L"SPIRV");
		args.push_back(L"-spirv");
		args.push_back(L"-fspv-target-env=vulkan1.2");
		args.push_back(L"-fvk-use-dx-layout");
		args.push_back(L"-fvk-use-dx-position-w");
		args.push_back(L"-fvk-t-shift"); args.push_back(L"1000"); args.push_back(L"0");
		args.push_back(L"-fvk-u-shift"); args.push_back(L"2000"); args.push_back(L"0");
		args.push_back(L"-fvk-s-shift"); args.push_back(L"3000"); args.push_back(L"0");

		// shader model	
		args.push_back(L"-T");
		switch (input.stage)
		{
		case ShaderStage::MS:
			args.push_back(L"ms_6_5");
			break;
		case ShaderStage::AS:
			args.push_back(L"as_6_5");
			break;
		case ShaderStage::VS:
			args.push_back(L"vs_6_0");
			break;
		case ShaderStage::HS:
			args.push_back(L"hs_6_0");
			break;
		case ShaderStage::DS:
			args.push_back(L"ds_6_0");
			break;
		case ShaderStage::GS:
			args.push_back(L"gs_6_0");
			break;
		case ShaderStage::PS:
			args.push_back(L"ps_6_0");
			break;
		case ShaderStage::CS:
			args.push_back(L"cs_6_0");
			break;
		case ShaderStage::LIB:
			args.push_back(L"lib_6_5");
			break;
		default:
			assert(0);
			return false;
		}

		std::vector<std::wstring> wstrings;
		wstrings.reserve(input.defines.size() + input.includeDirectories.size()); // keep ptr

		// defines
		for (auto& x : input.defines)
		{
			std::wstring& wstr = wstrings.emplace_back();
			Helper::StringConvert(x, wstr);
			args.push_back(L"-D");
			args.push_back(wstr.c_str());
		}

		// include directories
		for (auto& x : input.includeDirectories)
		{
			std::wstring& wstr = wstrings.emplace_back();
			Helper::StringConvert(x, wstr);
			args.push_back(L"-I");
			args.push_back(wstr.c_str());
		}

		// Entry point parameter:
		std::wstring wentry;
		Helper::StringConvert(input.entrypoint, wentry);
		args.push_back(L"-E");
		args.push_back(wentry.c_str());

		// Source file path
		std::wstring wsource;
		Helper::StringConvert(Helper::GetFileNameFromPath(input.shadersourcefilename), wsource);
		args.push_back(wsource.c_str());

		// Compile shader!!
		DxcBuffer Source;
		Source.Ptr = shadersourcedata.data();
		Source.Size = shadersourcedata.size();
		Source.Encoding = DXC_CP_ACP;

		IncludeHandler includeHandler;
		includeHandler.input = &input;
		includeHandler.output = &output;

		hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler.dxcIncludeHandler);
		assert(SUCCEEDED(hr));

		CComPtr<IDxcResult> pResults;
		hr = dxcCompiler->Compile(
			&Source,					// Source buffer.
			args.data(),                // Array of pointers to arguments.
			(UINT32)args.size(),		// Number of arguments.
			&includeHandler,		    // User-provided interface to handle #include directives (optional).
			IID_PPV_ARGS(&pResults)		// Compiler output status, buffer, and errors.
		);
		assert(SUCCEEDED(hr));

		// Print errors if present.
		CComPtr<IDxcBlobUtf8> pErrors = nullptr;
		hr = pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		assert(SUCCEEDED(hr));
		if (pErrors != nullptr && pErrors->GetStringLength() != 0)
			output.errorMessage = pErrors->GetStringPointer();

		// Quit if the compilation failed.
		HRESULT hrStatus;
		hr = pResults->GetStatus(&hrStatus);
		assert(SUCCEEDED(hr));
		if (FAILED(hrStatus))
			return false;

		// Save shader binary.
		CComPtr<IDxcBlob> pShader = nullptr;
		hr = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pShader), nullptr);
		assert(SUCCEEDED(hr));
		if (pShader != nullptr)
		{
			output.dependencies.push_back(input.shadersourcefilename);
			output.shaderdata = (const uint8_t*)pShader->GetBufferPointer();
			output.shadersize = pShader->GetBufferSize();

			// keep the blob alive == keep shader pointer valid!
			auto internalState = std::make_shared<CComPtr<IDxcBlob>>();
			*internalState = pShader;
			output.internalState = internalState;
			return true;
		}

		return false;
	}
}

}
}