/*
 * Native Vulkan integration for NVIDIA's DLSS Neural Rendering snippet.
 *
 * NVIDIA has not published a Streamline plugin or public feature definition
 * for this runtime yet.  The signed 310.8 snippet exposes the ordinary NGX
 * Vulkan ABI as feature 18, so this file deliberately isolates that evolving
 * contract from the supported Streamline DLSS SR/FG/Reflex implementation.
 */

#include "ref_import.h"
#include "tr_cvar.h"
#include "vk_streamline.h"
#include "vk_dlssnr.h"

#if defined(USE_NVIDIA_STREAMLINE) && defined(_WIN32) && defined(__x86_64__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_defs_vk.h>

namespace {

static constexpr unsigned long long kStreamlineTemporaryAppId = 100721531ULL;
static constexpr NVSDK_NGX_Feature kFeatureDlssNr =
	static_cast<NVSDK_NGX_Feature>(18);

using NgxInit = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long,
	const wchar_t *, VkInstance, VkPhysicalDevice, VkDevice,
	PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version,
	const NVSDK_NGX_Parameter *);
using NgxPopulate = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter *);
using NgxCreate = NVSDK_NGX_Result (NVSDK_CONV *)(VkCommandBuffer,
	NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using NgxEvaluate = NVSDK_NGX_Result (NVSDK_CONV *)(VkCommandBuffer,
	const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *,
	PFN_NVSDK_NGX_ProgressCallback_C);
using NgxRelease = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle *);
using NgxGetCapabilities = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter **);
using NgxDestroyParameters = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter *);

using BridgeInit = NVSDK_NGX_Result (NVSDK_CONV *)(NgxInit,
	unsigned long long, const wchar_t *, VkInstance, VkPhysicalDevice, VkDevice,
	PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, NVSDK_NGX_Version,
	const NVSDK_NGX_Parameter *);
using BridgePopulate = NVSDK_NGX_Result (NVSDK_CONV *)(NgxPopulate,
	NVSDK_NGX_Parameter *);
using BridgeCreate = NVSDK_NGX_Result (NVSDK_CONV *)(NgxCreate, VkCommandBuffer,
	NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
using BridgeEvaluate = NVSDK_NGX_Result (NVSDK_CONV *)(NgxEvaluate,
	VkCommandBuffer, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *,
	PFN_NVSDK_NGX_ProgressCallback_C);
using BridgeRelease = NVSDK_NGX_Result (NVSDK_CONV *)(NgxRelease,
	NVSDK_NGX_Handle *);

struct NeuralRenderingState {
	HMODULE snippet = nullptr;
	HMODULE bridge = nullptr;
	HMODULE core = nullptr; /* Owned by Streamline/the display driver. */
	NgxInit init = nullptr;
	NgxPopulate populate = nullptr;
	NgxCreate create = nullptr;
	NgxEvaluate evaluate = nullptr;
	NgxRelease release = nullptr;
	NgxGetCapabilities getCapabilities = nullptr;
	NgxDestroyParameters destroyParameters = nullptr;
	BridgeInit bridgeInit = nullptr;
	BridgePopulate bridgePopulate = nullptr;
	BridgeCreate bridgeCreate = nullptr;
	BridgeEvaluate bridgeEvaluate = nullptr;
	BridgeRelease bridgeRelease = nullptr;
	NVSDK_NGX_Parameter *parameters = nullptr;
	NVSDK_NGX_Handle *feature = nullptr;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
	int mode = 0;
	bool attached = false;
	bool failed = false;
	bool evaluationLogged = false;
};

NeuralRenderingState g_nr;

/*
 * NVSDK_NGX_Parameter is a C++ virtual interface implemented by MSVC-built
 * NVIDIA modules. MinGW and MSVC order overloaded virtuals differently, so
 * invoking it directly from this renderer can dispatch to the wrong method.
 * Dispatch through the MSVC vtable slots documented by NVIDIA's own C ABI
 * wrappers. Windows x64 has one calling convention, so the function boundary
 * itself is compiler-neutral.
 */
class ParameterWriter {
public:
	explicit ParameterWriter(NVSDK_NGX_Parameter *inner) : inner_(inner) {}
	void Set(const char *n, unsigned long long v) { call<void, unsigned long long>(0x38, n, v); }
	void Set(const char *n, float v) { call<void, float>(0x30, n, v); }
	void Set(const char *n, double v) { call<void, double>(0x28, n, v); }
	void Set(const char *n, unsigned int v) { call<void, unsigned int>(0x20, n, v); }
	void Set(const char *n, int v) { call<void, int>(0x18, n, v); }
	void Set(const char *n, void *v) { call<void, void *>(0x00, n, v); }
	void Reset()
	{
		using Function = void (NVSDK_CONV *)(NVSDK_NGX_Parameter *);
		auto **vtable = *reinterpret_cast<void ***>(inner_);
		reinterpret_cast<Function>(vtable[0x80 / sizeof(void *)])(inner_);
	}

private:
	template <typename Return, typename Value>
	Return call(size_t byteOffset, const char *name, Value value)
	{
		using Function = Return (NVSDK_CONV *)(NVSDK_NGX_Parameter *,
			const char *, Value);
		auto **vtable = *reinterpret_cast<void ***>(inner_);
		return reinterpret_cast<Function>(
			vtable[byteOffset / sizeof(void *)])(inner_, name, value);
	}
	NVSDK_NGX_Parameter *inner_;
};

std::wstring executableDirectory()
{
	std::wstring path(32768, L'\0');
	DWORD length = GetModuleFileNameW(nullptr, &path[0],
		static_cast<DWORD>(path.size()));
	if (!length || length >= path.size()) return std::wstring();
	path.resize(length);
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

template <typename T>
bool loadExport(HMODULE module, T &function, const char *name)
{
	function = reinterpret_cast<T>(GetProcAddress(module, name));
	return function != nullptr;
}

void releaseFeature()
{
	if (g_nr.feature && g_nr.bridgeRelease && g_nr.release) {
		const NVSDK_NGX_Result result =
			g_nr.bridgeRelease(g_nr.release, g_nr.feature);
		if (NVSDK_NGX_FAILED(result))
			ri.Printf(PRINT_WARNING,
				"NVIDIA DLSS Neural Rendering release failed (0x%08x)\n",
				(unsigned)result);
	}
	g_nr.feature = nullptr;
}

void resetModules()
{
	if (g_nr.parameters && g_nr.destroyParameters)
		g_nr.destroyParameters(g_nr.parameters);
	g_nr.parameters = nullptr;
	if (g_nr.bridge) FreeLibrary(g_nr.bridge);
	if (g_nr.snippet) FreeLibrary(g_nr.snippet);
	g_nr = NeuralRenderingState{};
}

void setCreationParameters()
{
	ParameterWriter writer(g_nr.parameters);
	ParameterWriter *p = &writer;
	p->Reset();
	g_nr.bridgePopulate(g_nr.populate, g_nr.parameters);
	const unsigned int width = g_nr.width;
	const unsigned int height = g_nr.height;
	const unsigned int style = static_cast<unsigned int>(g_nr.mode - 1);
	const int featureFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
		NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

	p->Set("CreationNodeMask", 1u);
	p->Set("VisibilityNodeMask", 1u);
	p->Set("Width", width); p->Set("Height", height);
	p->Set("OutWidth", width); p->Set("OutHeight", height);
	p->Set("ResourceWidth", width); p->Set("ResourceHeight", height);
	p->Set("ResourceOutWidth", width); p->Set("ResourceOutHeight", height);
	p->Set("PerfQualityValue", static_cast<int>(NVSDK_NGX_PerfQuality_Value_UltraQuality));
	p->Set("DLSS.Feature.Create.Flags", featureFlags);
	p->Set("DLSS.Enable.Output.Subrects", 0);
	p->Set("DLSS.Denoise.Mode", 1);
	p->Set("DLSS.Roughness.Mode", 0u);
	p->Set("DLSS.Use.HW.Depth", 1u);
	p->Set("DLSSNR.Enabled", 1u);
	p->Set("DLSSNR.InputWidth", width); p->Set("DLSSNR.InputHeight", height);
	p->Set("DLSSNR.Width", width); p->Set("DLSSNR.Height", height);
	p->Set("DLSSNR.OutputWidth", width); p->Set("DLSSNR.OutputHeight", height);
	p->Set("Output.Width", width); p->Set("Output.Height", height);
	p->Set("DLSSNR.Upscaling", 1u);
	p->Set("DLSSNR.ScalingRatio", 1.0f); p->Set("DLSSNR.Scale", 1.0f);
	p->Set("DLSSNR.Hint.Render.Preset", g_nr.mode);
	p->Set("DLSSNR.Style", style);
	p->Set("DLSSNR.Intensity", r_dlssNRIntensity->value);
	p->Set("DLSSNR.LocalToneStrength", r_dlssNRLocalToneStrength->value);
	p->Set("DLSSNR.LocalStructureStrength", r_dlssNRLocalStructureStrength->value);
	p->Set("DLSSNR.SkinStructureStrength", r_dlssNRSkinStructureStrength->value);
	p->Set("DLSSNR.UseAutoMask", 1u);
	p->Set("DLSSNR.UICorrection", 0u);
}

NVSDK_NGX_Resource_VK makeImageResource(VkImage image, VkImageView view,
	VkFormat format, uint32_t width, uint32_t height,
	VkImageAspectFlags aspect, bool readWrite)
{
	NVSDK_NGX_Resource_VK resource{};
	resource.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
	resource.ReadWrite = readWrite;
	resource.Resource.ImageViewInfo.ImageView = view;
	resource.Resource.ImageViewInfo.Image = image;
	resource.Resource.ImageViewInfo.Format = format;
	resource.Resource.ImageViewInfo.Width = width;
	resource.Resource.ImageViewInfo.Height = height;
	resource.Resource.ImageViewInfo.SubresourceRange.aspectMask = aspect;
	resource.Resource.ImageViewInfo.SubresourceRange.baseMipLevel = 0;
	resource.Resource.ImageViewInfo.SubresourceRange.levelCount = 1;
	resource.Resource.ImageViewInfo.SubresourceRange.baseArrayLayer = 0;
	resource.Resource.ImageViewInfo.SubresourceRange.layerCount = 1;
	return resource;
}

void setEvaluationParameters(const vk_sl_frame_resources_t *r,
	NVSDK_NGX_Resource_VK *color, NVSDK_NGX_Resource_VK *output,
	NVSDK_NGX_Resource_VK *depth, NVSDK_NGX_Resource_VK *motion)
{
	ParameterWriter writer(g_nr.parameters);
	ParameterWriter *p = &writer;
	const unsigned int width = r->render_width;
	const unsigned int height = r->render_height;
	const unsigned int style = static_cast<unsigned int>(g_nr.mode - 1);
	const int reset = r->reset ? 1 : 0;

	p->Set("Color", color); p->Set("DLSSNR.Color", color);
	p->Set("Output", output); p->Set("DLSSNR.Output", output);
	p->Set("Depth", depth); p->Set("DLSSNR.Depth", depth);
	p->Set("MotionVectors", motion); p->Set("DLSSNR.MVec", motion);
	p->Set("Reset", reset); p->Set("DLSSNR.Reset", reset);
	p->Set("Jitter.Offset.X", r->jitter_x); p->Set("Jitter.Offset.Y", r->jitter_y);
	p->Set("MV.Scale.X", 1.0f); p->Set("MV.Scale.Y", 1.0f);
	p->Set("DLSSNR.JitterOffsetX", r->jitter_x);
	p->Set("DLSSNR.JitterOffsetY", r->jitter_y);
	p->Set("DLSSNR.MVecScaleX", 1.0f); p->Set("DLSSNR.MVecScaleY", 1.0f);
	p->Set("DLSS.Pre.Exposure", 1.0f); p->Set("DLSS.Exposure.Scale", 1.0f);
	p->Set("DLSS.Render.Subrect.Dimensions.Width", width);
	p->Set("DLSS.Render.Subrect.Dimensions.Height", height);
	p->Set("DLSS.Input.Color.Subrect.Base.X", 0u);
	p->Set("DLSS.Input.Color.Subrect.Base.Y", 0u);
	p->Set("DLSS.Input.Depth.Subrect.Base.X", 0u);
	p->Set("DLSS.Input.Depth.Subrect.Base.Y", 0u);
	p->Set("DLSS.Input.MV.Subrect.Base.X", 0u);
	p->Set("DLSS.Input.MV.Subrect.Base.Y", 0u);
	p->Set("DLSS.Output.Subrect.Base.X", 0u);
	p->Set("DLSS.Output.Subrect.Base.Y", 0u);

	const char *subrects[] = { "Color", "MVec", "Depth", "Output" };
	for (const char *name : subrects) {
		std::string prefix = std::string("DLSSNR.") + name + "Subrect";
		p->Set((prefix + "BaseX").c_str(), 0);
		p->Set((prefix + "BaseY").c_str(), 0);
		p->Set((prefix + "Width").c_str(), static_cast<int>(width));
		p->Set((prefix + "Height").c_str(), static_cast<int>(height));
	}
	const char *optionalResources[] = { "ControlMask", "UI", "UIAlpha",
		"Backbuffer", "BidirectionalDistortionField" };
	for (const char *name : optionalResources) {
		std::string key = std::string("DLSSNR.") + name;
		p->Set(key.c_str(), static_cast<void *>(nullptr));
		p->Set((key + "SubrectBaseX").c_str(), 0);
		p->Set((key + "SubrectBaseY").c_str(), 0);
		p->Set((key + "SubrectWidth").c_str(), static_cast<int>(width));
		p->Set((key + "SubrectHeight").c_str(), static_cast<int>(height));
	}
	p->Set("DLSSNR.DepthInverted", 0u);
	p->Set("DLSSNR.InputWidth", width); p->Set("DLSSNR.InputHeight", height);
	p->Set("DLSSNR.Width", width); p->Set("DLSSNR.Height", height);
	p->Set("DLSSNR.OutputWidth", width); p->Set("DLSSNR.OutputHeight", height);
	p->Set("DLSSNR.Upscaling", 1u);
	p->Set("DLSSNR.ScalingRatio", 1.0f); p->Set("DLSSNR.Scale", 1.0f);
	p->Set("DLSSNR.Hint.Render.Preset", g_nr.mode);
	p->Set("DLSSNR.Style", style);
	p->Set("DLSSNR.Intensity", r_dlssNRIntensity->value);
	p->Set("DLSSNR.LocalToneStrength", r_dlssNRLocalToneStrength->value);
	p->Set("DLSSNR.LocalStructureStrength", r_dlssNRLocalStructureStrength->value);
	p->Set("DLSSNR.SkinStructureStrength", r_dlssNRSkinStructureStrength->value);
	p->Set("DLSSNR.Enabled", 1u);
	p->Set("DLSSNR.UseAutoMask", 1u);
	p->Set("DLSSNR.UICorrection", 0u);
}

} // namespace

extern "C" qboolean vk_dlssnr_attach(VkInstance instance,
	VkPhysicalDevice physicalDevice, VkDevice device,
	PFN_vkGetInstanceProcAddr getInstanceProcAddr,
	PFN_vkGetDeviceProcAddr getDeviceProcAddr)
{
	if (g_nr.attached) return qtrue;
	const std::wstring directory = executableDirectory();
	if (directory.empty()) return qfalse;
	const std::wstring nrPath = directory + L"\\nvngx_dlssnr.dll";
	const std::wstring bridgePath = directory + L"\\nvngx.dll";
	if (GetFileAttributesW(nrPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		ri.Printf(PRINT_ALL,
			"NVIDIA DLSS Neural Rendering runtime not installed (nvngx_dlssnr.dll)\n");
		return qfalse;
	}
	if (GetFileAttributesW(bridgePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering bridge not installed (nvngx.dll)\n");
		return qfalse;
	}
	if (!vk_sl_verify_nvidia_signature(nrPath.c_str())) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering runtime signature is invalid\n");
		return qfalse;
	}

	/* Streamline initialized the driver NGX core before creating the device. */
	g_nr.core = GetModuleHandleW(L"_nvngx.dll");
	if (!g_nr.core) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering could not find the initialized NGX driver core\n");
		return qfalse;
	}
	g_nr.snippet = LoadLibraryExW(nrPath.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	g_nr.bridge = LoadLibraryExW(bridgePath.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!g_nr.snippet || !g_nr.bridge ||
		!loadExport(g_nr.snippet, g_nr.init, "NVSDK_NGX_VULKAN_Init_Ext2") ||
		!loadExport(g_nr.snippet, g_nr.populate, "NVSDK_NGX_VULKAN_PopulateParameters_Impl") ||
		!loadExport(g_nr.snippet, g_nr.create, "NVSDK_NGX_VULKAN_CreateFeature") ||
		!loadExport(g_nr.snippet, g_nr.evaluate, "NVSDK_NGX_VULKAN_EvaluateFeature") ||
		!loadExport(g_nr.snippet, g_nr.release, "NVSDK_NGX_VULKAN_ReleaseFeature") ||
		!loadExport(g_nr.core, g_nr.getCapabilities, "NVSDK_NGX_VULKAN_GetCapabilityParameters") ||
		!loadExport(g_nr.core, g_nr.destroyParameters, "NVSDK_NGX_VULKAN_DestroyParameters") ||
		!loadExport(g_nr.bridge, g_nr.bridgeInit, "NVNGXBridge_VULKAN_InitExt2") ||
		!loadExport(g_nr.bridge, g_nr.bridgePopulate, "NVNGXBridge_VULKAN_PopulateParameters") ||
		!loadExport(g_nr.bridge, g_nr.bridgeCreate, "NVNGXBridge_VULKAN_CreateFeature") ||
		!loadExport(g_nr.bridge, g_nr.bridgeEvaluate, "NVNGXBridge_VULKAN_EvaluateFeature") ||
		!loadExport(g_nr.bridge, g_nr.bridgeRelease, "NVNGXBridge_VULKAN_ReleaseFeature")) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering runtime is missing a required Vulkan entry point\n");
		resetModules();
		return qfalse;
	}
	NVSDK_NGX_Result result = g_nr.bridgeInit(g_nr.init,
		kStreamlineTemporaryAppId, directory.c_str(), instance, physicalDevice,
		device, getInstanceProcAddr, getDeviceProcAddr, NVSDK_NGX_Version_API,
		nullptr);
	if (NVSDK_NGX_FAILED(result)) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering snippet initialization failed (0x%08x)\n",
			(unsigned)result);
		resetModules();
		return qfalse;
	}
	result = g_nr.getCapabilities(&g_nr.parameters);
	if (NVSDK_NGX_FAILED(result) || !g_nr.parameters) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering could not acquire NGX parameters (0x%08x)\n",
			(unsigned)result);
		resetModules();
		return qfalse;
	}
	result = g_nr.bridgePopulate(g_nr.populate, g_nr.parameters);
	if (NVSDK_NGX_FAILED(result)) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering parameter initialization failed (0x%08x)\n",
			(unsigned)result);
		resetModules();
		return qfalse;
	}

	g_nr.device = device;
	g_nr.attached = true;
	ri.Printf(PRINT_ALL,
		"NVIDIA DLSS Neural Rendering 310.8 runtime attached (experimental feature 18)\n");
	return qtrue;
}

extern "C" void vk_dlssnr_shutdown(void)
{
	releaseFeature();
}

extern "C" void vk_dlssnr_unload(void)
{
	releaseFeature();
	resetModules();
}

extern "C" qboolean vk_dlssnr_supported(void)
{
	return g_nr.attached && !g_nr.failed ? qtrue : qfalse;
}

extern "C" void vk_dlssnr_configure(int mode, uint32_t width, uint32_t height)
{
	if (mode < 0) mode = 0;
	if (mode > 3) mode = 3;
	if (g_nr.mode != mode || g_nr.width != width || g_nr.height != height)
		releaseFeature();
	g_nr.mode = mode;
	g_nr.width = width;
	g_nr.height = height;
	g_nr.failed = false;
	g_nr.evaluationLogged = false;
}

extern "C" qboolean vk_dlssnr_prepare(VkCommandBuffer commandBuffer)
{
	if (!g_nr.attached || g_nr.failed || !g_nr.mode || !g_nr.parameters ||
		commandBuffer == VK_NULL_HANDLE)
		return qfalse;
	if (g_nr.feature)
		return qtrue;

	setCreationParameters();
	const NVSDK_NGX_Result result = g_nr.bridgeCreate(g_nr.create,
		commandBuffer, kFeatureDlssNr, g_nr.parameters, &g_nr.feature);
	if (NVSDK_NGX_FAILED(result) || !g_nr.feature) {
		ri.Printf(PRINT_WARNING,
			"NVIDIA DLSS Neural Rendering feature creation failed (0x%08x)\n",
			(unsigned)result);
		g_nr.failed = true;
		ri.Cvar_Set("r_dlssNeuralRenderingAvailable", "0");
		return qfalse;
	}
	ri.Printf(PRINT_ALL,
		"NVIDIA DLSS Neural Rendering feature created: %ux%u, model %d\n",
		g_nr.width, g_nr.height, g_nr.mode);
	return qtrue;
}

extern "C" qboolean vk_dlssnr_evaluate(const vk_sl_frame_resources_t *r)
{
	if (!r || !g_nr.attached || g_nr.failed || !g_nr.mode ||
		!g_nr.parameters || !r->color_input || !r->color_output)
		return qfalse;
	if (r->render_width != g_nr.width || r->render_height != g_nr.height)
		return qfalse;

	if (!g_nr.feature && !vk_dlssnr_prepare(r->command_buffer))
		return qfalse;

	NVSDK_NGX_Resource_VK color = makeImageResource(r->color_input,
		r->color_input_view, r->color_format, r->render_width,
		r->render_height, VK_IMAGE_ASPECT_COLOR_BIT, false);
	NVSDK_NGX_Resource_VK output = makeImageResource(r->color_output,
		r->color_output_view, r->color_format, r->render_width,
		r->render_height, VK_IMAGE_ASPECT_COLOR_BIT, true);
	NVSDK_NGX_Resource_VK depth = makeImageResource(r->depth, r->depth_view,
		r->depth_format, r->render_width, r->render_height,
		VK_IMAGE_ASPECT_DEPTH_BIT, false);
	NVSDK_NGX_Resource_VK motion = makeImageResource(r->motion_vectors,
		r->motion_vectors_view, r->motion_vectors_format, r->render_width,
		r->render_height, VK_IMAGE_ASPECT_COLOR_BIT, false);
	setEvaluationParameters(r, &color, &output, &depth, &motion);
	const NVSDK_NGX_Result result = g_nr.bridgeEvaluate(g_nr.evaluate,
		r->command_buffer, g_nr.feature, g_nr.parameters, nullptr);
	if (NVSDK_NGX_FAILED(result)) {
		if (!g_nr.evaluationLogged)
			ri.Printf(PRINT_WARNING,
				"NVIDIA DLSS Neural Rendering evaluation failed (0x%08x); using the original scene\n",
				(unsigned)result);
		g_nr.evaluationLogged = true;
		return qfalse;
	}
	if (!g_nr.evaluationLogged)
		ri.Printf(PRINT_ALL,
			"NVIDIA DLSS Neural Rendering evaluation active "
			"(intensity %.2f, local tone %.2f, local structure %.2f, skin structure %.2f)\n",
			r_dlssNRIntensity->value, r_dlssNRLocalToneStrength->value,
			r_dlssNRLocalStructureStrength->value,
			r_dlssNRSkinStructureStrength->value);
	g_nr.evaluationLogged = true;
	return qtrue;
}

#else

extern "C" qboolean vk_dlssnr_attach(VkInstance, VkPhysicalDevice, VkDevice,
	PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr) { return qfalse; }
extern "C" void vk_dlssnr_shutdown(void) {}
extern "C" void vk_dlssnr_unload(void) {}
extern "C" qboolean vk_dlssnr_supported(void) { return qfalse; }
extern "C" void vk_dlssnr_configure(int, uint32_t, uint32_t) {}
extern "C" qboolean vk_dlssnr_prepare(VkCommandBuffer) { return qfalse; }
extern "C" qboolean vk_dlssnr_evaluate(const vk_sl_frame_resources_t *) { return qfalse; }

#endif
