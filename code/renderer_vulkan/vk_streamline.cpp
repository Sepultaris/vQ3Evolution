/*
 * NVIDIA Streamline integration boundary for the Vulkan renderer.
 *
 * Streamline is loaded dynamically so non-NVIDIA systems and installations
 * without the optional runtime continue to use the ordinary Vulkan path.
 */

#include "ref_import.h"
#include "vk_streamline.h"
#include "vk_dlssnr.h"

#if defined(USE_NVIDIA_STREAMLINE) && defined(_WIN32) && defined(__x86_64__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>

#include <algorithm>
#include <string>
#include <vector>

#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_matrix_helpers.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_security.h>
#include <sl_helpers_vk.h>

namespace {

struct StreamlineState {
	HMODULE module = nullptr;
	PFun_slInit *init = nullptr;
	PFun_slShutdown *shutdown = nullptr;
	PFun_slIsFeatureSupported *isFeatureSupported = nullptr;
	PFun_slGetFeatureRequirements *getFeatureRequirements = nullptr;
	PFun_slGetFeatureFunction *getFeatureFunction = nullptr;
	PFun_slSetFeatureLoaded *setFeatureLoaded = nullptr;
	PFun_slSetVulkanInfo *setVulkanInfo = nullptr;
	PFun_slSetTagForFrame *setTagForFrame = nullptr;
	PFun_slSetConstants *setConstants = nullptr;
	PFun_slEvaluateFeature *evaluateFeature = nullptr;
	PFun_slFreeResources *freeResources = nullptr;
	PFun_slGetNewFrameToken *getNewFrameToken = nullptr;
	PFun_slDLSSSetOptions *dlssSetOptions = nullptr;
	PFun_slDLSSGetOptimalSettings *dlssGetOptimalSettings = nullptr;
	PFun_slDLSSDSetOptions *rrSetOptions = nullptr;
	PFun_slDLSSDGetOptimalSettings *rrGetOptimalSettings = nullptr;
	bool rrRequirementsAvailable = false;
	bool rrSupported = false, rrEnabled = false, rrAttempted = false;
	uint32_t rrFrames = 0;
	sl::DLSSDOptions rrOptions {};
	PFun_slDLSSGSetOptions *dlssGSetOptions = nullptr;
	PFun_slDLSSGGetState *dlssGGetState = nullptr;
	PFun_slReflexSetOptions *reflexSetOptions = nullptr;
	PFun_slReflexSleep *reflexSleep = nullptr;
	PFun_slPCLSetMarker *pclSetMarker = nullptr;
	PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
	PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
	std::vector<const char *> instanceExtensions;
	std::vector<const char *> deviceExtensions;
	bool initialized = false;
	bool dlssSupported = false;
	bool frameGenerationSupported = false;
	bool frameGenerationHooksUnloaded = false;
	bool frameGenerationPresentationSupported = false;
	bool reflexSupported = false;
	bool dlssEnabled = false;
	bool dlssEvaluationAttempted = false;
	float ptScale = 1.0f;
	bool ptScaleModeMatched = false;
	bool frameGenerationEnabled = false;
	bool frameGenerationActive = false;
	bool frameGenerationResourcesAllocated = false;
	sl::FrameToken *frameToken = nullptr;
	sl::float4x4 previousCameraToWorld {};
	sl::float4x4 previousProjection {};
	bool havePreviousCamera = false;
	bool frameGenerationStateLogged = false;
	uint32_t frameGenerationQueries = 0;
	uint32_t frameGenerationPresented = 0;
	uint32_t frameGenerationPeakPresented = 0;
	uint32_t frameGenerationUnfocusedQueries = 0;
	uint32_t frameGenerationMinimizedQueries = 0;
	uint32_t frameGenerationFailedQueries = 0;
	uint32_t constantFrames = 0;
	uint32_t constantResets = 0;
	sl::Result frameGenerationResult = sl::Result::eOk;
	sl::DLSSGStatus frameGenerationStatus = sl::DLSSGStatus::eOk;
	sl::DLSSGOptions dlssGOptions {};
	std::string status = "Streamline is not initialized";
};

StreamlineState g_sl;

void logMessage(sl::LogType type, const char *message)
{
	int level = type == sl::LogType::eError ? PRINT_WARNING : PRINT_DEVELOPER;
	ri.Printf(level, "Streamline: %s", message ? message : "(empty message)\n");
}

std::wstring executableDirectory()
{
	std::wstring path(32768, L'\0');
	DWORD length = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
	if (!length || length >= path.size()) {
		return std::wstring();
	}
	path.resize(length);
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

template <typename T>
bool loadFunction(T *&function, const char *name)
{
	function = reinterpret_cast<T *>(GetProcAddress(g_sl.module, name));
	if (!function) {
		g_sl.status = std::string("Streamline runtime is missing ") + name;
		return false;
	}
	return true;
}

template <typename T>
void *nativeHandle(T handle)
{
	return reinterpret_cast<void *>(handle);
}

void copyColumnMajorToRowMajor(sl::float4x4 &out, const float *in)
{
	float *dst = &out[0].x;
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
			dst[row * 4 + column] = in[column * 4 + row];
}

void setIdentity(sl::float4x4 &matrix)
{
	float *p = &matrix[0].x;
	std::fill(p, p + 16, 0.0f);
	p[0] = p[5] = p[10] = p[15] = 1.0f;
}

void describeResource(sl::Resource &resource, uint32_t width, uint32_t height,
	VkFormat format, VkImageUsageFlags usage)
{
	resource.width = width;
	resource.height = height;
	resource.nativeFormat = static_cast<uint32_t>(format);
	resource.mipLevels = 1;
	resource.arrayLayers = 1;
	resource.usage = static_cast<uint32_t>(usage);
}

void appendUnique(std::vector<const char *> &extensions, const char *name)
{
	if (!name || !name[0]) {
		return;
	}
	for (const char *existing : extensions) {
		if (!strcmp(existing, name)) {
			return;
		}
	}
	extensions.push_back(name);
}

void queryFrameGenerationState()
{
	if (!g_sl.frameGenerationActive || !g_sl.dlssGGetState)
		return;

	sl::DLSSGState state;
	const sl::Result result = g_sl.dlssGGetState(sl::ViewportHandle(0), state,
		nullptr);
	++g_sl.frameGenerationQueries;
	g_sl.frameGenerationUnfocusedQueries += !!ri.Cvar_VariableIntegerValue("com_unfocused");
	g_sl.frameGenerationMinimizedQueries += !!ri.Cvar_VariableIntegerValue("com_minimized");
	g_sl.frameGenerationFailedQueries += result != sl::Result::eOk || state.status != sl::DLSSGStatus::eOk;
	g_sl.frameGenerationResult = result;
	g_sl.frameGenerationStatus = state.status;
	if (result == sl::Result::eOk) {
		g_sl.frameGenerationPresented += state.numFramesActuallyPresented;
		g_sl.frameGenerationPeakPresented = std::max(g_sl.frameGenerationPeakPresented,
			state.numFramesActuallyPresented);
	}
	if (!g_sl.frameGenerationStateLogged) {
		if (result == sl::Result::eOk && state.status == sl::DLSSGStatus::eOk &&
			state.numFramesActuallyPresented > 1)
			ri.Printf(PRINT_ALL, "NVIDIA DLSS Frame Generation active (%u frames presented)\n",
				state.numFramesActuallyPresented);
		else if (result != sl::Result::eOk || state.status != sl::DLSSGStatus::eOk)
			ri.Printf(PRINT_WARNING, "NVIDIA DLSS Frame Generation runtime status 0x%x (result %d)\n",
				(unsigned)state.status, (int)result);
		g_sl.frameGenerationStateLogged = state.numFramesActuallyPresented > 1 ||
			result != sl::Result::eOk || state.status != sl::DLSSGStatus::eOk;
	}
}

bool collectRequirements(sl::Feature feature)
{
	sl::FeatureRequirements requirements;
	const sl::Result result = g_sl.getFeatureRequirements(feature, requirements);
	if (result != sl::Result::eOk) {
		g_sl.status = "Streamline could not query Vulkan feature requirements";
		return false;
	}
	if (!(requirements.flags & sl::FeatureRequirementFlags::eVulkanSupported)) {
		g_sl.status = "An installed Streamline feature does not support Vulkan";
		return false;
	}
	for (uint32_t i = 0; i < requirements.vkNumInstanceExtensions; ++i) {
		appendUnique(g_sl.instanceExtensions, requirements.vkInstanceExtensions[i]);
	}
	for (uint32_t i = 0; i < requirements.vkNumDeviceExtensions; ++i) {
		appendUnique(g_sl.deviceExtensions, requirements.vkDeviceExtensions[i]);
	}
	return true;
}

void unloadIdleFrameGenerationHooks()
{
	// This runs once on the newly created device, after support/interface queries
	// and BEFORE the surface/swapchain or any rendered submissions exist. FG is
	// latched: changing it already rebuilds the renderer/device and swapchain.
	// Streamline's DLSS-G guide section 18 requires this unhooked swapchain when
	// FG is off, otherwise an extra off-screen copy and presentation queue remain.
	if (!g_sl.frameGenerationSupported || ri.Cvar_VariableIntegerValue("r_dlssFrameGeneration") ||
		ri.Cvar_VariableIntegerValue("r_dlssFGIdleHooks")) return;
	if (!g_sl.setFeatureLoaded) {
		ri.Printf(PRINT_WARNING, "NVIDIA idle FG hooks retained: slSetFeatureLoaded is unavailable\n");
		return;
	}
	const sl::Result result = g_sl.setFeatureLoaded(sl::kFeatureDLSS_G, false);
	if (result != sl::Result::eOk) {
		ri.Printf(PRINT_WARNING, "NVIDIA idle FG hooks retained: unload returned %d\n", (int)result);
		return;
	}
	// Capability stays available in the menu; Apply restarts into a loaded FG
	// plugin. Never invoke feature interfaces while that feature is unloaded.
	g_sl.frameGenerationHooksUnloaded = true;
	g_sl.dlssGSetOptions = nullptr;
	g_sl.dlssGGetState = nullptr;
	ri.Printf(PRINT_ALL, "NVIDIA idle FG presentation hooks unloaded before swapchain creation\n");
}

void clearState(bool unload)
{
	if (unload && g_sl.module) {
		FreeLibrary(g_sl.module);
	}
	g_sl = StreamlineState{};
}

} // namespace

extern "C" qboolean vk_sl_verify_nvidia_signature(const wchar_t *path)
{
	if (!path || !path[0]) return qfalse;

	WINTRUST_FILE_INFO fileInfo{};
	fileInfo.cbStruct = sizeof(fileInfo);
	fileInfo.pcwszFilePath = path;
	WINTRUST_DATA trustData{};
	trustData.cbStruct = sizeof(trustData);
	trustData.dwUIChoice = WTD_UI_NONE;
	trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
	trustData.dwUnionChoice = WTD_CHOICE_FILE;
	trustData.pFile = &fileInfo;
	trustData.dwStateAction = WTD_STATEACTION_VERIFY;
	trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
	GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	const LONG trustResult = WinVerifyTrust(nullptr, &policy, &trustData);
	trustData.dwStateAction = WTD_STATEACTION_CLOSE;
	WinVerifyTrust(nullptr, &policy, &trustData);
	if (trustResult != ERROR_SUCCESS) return qfalse;

	DWORD encoding = 0, content = 0, format = 0;
	HCERTSTORE store = nullptr;
	HCRYPTMSG message = nullptr;
	if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path,
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
		CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content, &format,
		&store, &message, nullptr))
		return qfalse;

	DWORD signerSize = 0;
	qboolean valid = qfalse;
	if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr,
		&signerSize) && signerSize) {
		PCMSG_SIGNER_INFO signer = static_cast<PCMSG_SIGNER_INFO>(
			LocalAlloc(LPTR, signerSize));
		if (signer && CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0,
			signer, &signerSize)) {
			CERT_INFO certInfo{};
			certInfo.Issuer = signer->Issuer;
			certInfo.SerialNumber = signer->SerialNumber;
			PCCERT_CONTEXT certificate = CertFindCertificateInStore(store,
				X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
				CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
			if (certificate) {
				wchar_t publisher[256]{};
				CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE,
					0, nullptr, publisher, static_cast<DWORD>(std::size(publisher)));
				valid = wcscmp(publisher, L"NVIDIA Corporation") == 0 ? qtrue : qfalse;
				CertFreeCertificateContext(certificate);
			}
		}
		if (signer) LocalFree(signer);
	}
	CryptMsgClose(message);
	CertCloseStore(store, 0);
	return valid;
}

extern "C" qboolean vk_sl_initialize(void)
{
	if (g_sl.initialized) {
		return qtrue;
	}

	const std::wstring directory = executableDirectory();
	if (directory.empty()) {
		g_sl.status = "Could not determine the executable directory";
		return qfalse;
	}
	const std::wstring interposer = directory + L"\\sl.interposer.dll";
	if (GetFileAttributesW(interposer.c_str()) == INVALID_FILE_ATTRIBUTES) {
		g_sl.status = "Optional NVIDIA Streamline runtime is not installed beside the executable";
		return qfalse;
	}

	/* NVIDIA requires production integrations to reject modified or unsigned
	 * interposers before loading any code from them. */
	if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
		g_sl.status = "The NVIDIA Streamline runtime signature is invalid";
		return qfalse;
	}

	/* Absolute path plus restricted dependency lookup avoids DLL search-order hijacking. */
	g_sl.module = LoadLibraryExW(interposer.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!g_sl.module) {
		g_sl.status = "Optional NVIDIA Streamline runtime is not installed beside the executable";
		return qfalse;
	}

	if (!loadFunction(g_sl.init, "slInit") ||
		!loadFunction(g_sl.shutdown, "slShutdown") ||
		!loadFunction(g_sl.isFeatureSupported, "slIsFeatureSupported") ||
		!loadFunction(g_sl.getFeatureRequirements, "slGetFeatureRequirements") ||
		!loadFunction(g_sl.getFeatureFunction, "slGetFeatureFunction") ||
		!loadFunction(g_sl.setTagForFrame, "slSetTagForFrame") ||
		!loadFunction(g_sl.setConstants, "slSetConstants") ||
		!loadFunction(g_sl.evaluateFeature, "slEvaluateFeature") ||
		!loadFunction(g_sl.freeResources, "slFreeResources") ||
		!loadFunction(g_sl.getNewFrameToken, "slGetNewFrameToken") ||
		!loadFunction(g_sl.setVulkanInfo, "slSetVulkanInfo")) {
		clearState(true);
		return qfalse;
	}
	g_sl.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
		GetProcAddress(g_sl.module, "vkGetInstanceProcAddr"));
	g_sl.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
		GetProcAddress(g_sl.module, "vkGetDeviceProcAddr"));
	g_sl.setFeatureLoaded = reinterpret_cast<PFun_slSetFeatureLoaded *>(
		GetProcAddress(g_sl.module, "slSetFeatureLoaded"));
	if (!g_sl.getInstanceProcAddr || !g_sl.getDeviceProcAddr) {
		g_sl.status = "Streamline runtime does not export the Vulkan entry points";
		clearState(true);
		return qfalse;
	}

	static const sl::Feature features[] = {
		sl::kFeatureReflex,
		sl::kFeatureDLSS,
		sl::kFeatureDLSS_RR,
		sl::kFeatureDLSS_G
	};
	sl::Preferences preferences;
	preferences.logLevel = sl::LogLevel::eDefault;
	preferences.logMessageCallback = logMessage;
	preferences.flags = sl::PreferenceFlags::eUseManualHooking |
		sl::PreferenceFlags::eDisableCLStateTracking |
		sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	preferences.featuresToLoad = features;
	preferences.numFeaturesToLoad = static_cast<uint32_t>(std::size(features));
	preferences.engine = sl::EngineType::eCustom;
	preferences.engineVersion = PRODUCT_VERSION;
	/* Stable, project-owned GUID used by NGX's custom-engine identity path. */
	preferences.projectId = "66b8e39a-d95f-4ed1-a032-dab9717bf73f";
	preferences.renderAPI = sl::RenderAPI::eVulkan;

	const sl::Result result = g_sl.init(preferences, sl::kSDKVersion);
	if (result != sl::Result::eOk) {
		g_sl.status = "NVIDIA Streamline initialization failed";
		clearState(true);
		return qfalse;
	}
	g_sl.initialized = true;

	bool requirementsOk = true;
	for (const sl::Feature feature : features) {
		const bool available = collectRequirements(feature);
		// RR is optional: a missing RR plugin must not disable SR/Reflex.
		if (feature == sl::kFeatureDLSS_RR) g_sl.rrRequirementsAvailable = available;
		else requirementsOk = available && requirementsOk;
	}
	if (!requirementsOk) {
		vk_sl_shutdown();
		return qfalse;
	}

	g_sl.status = "Streamline initialized; waiting for Vulkan device capability check";
	ri.Printf(PRINT_ALL, "NVIDIA Streamline %u.%u.%u initialized\n",
		SL_VERSION_MAJOR, SL_VERSION_MINOR, SL_VERSION_PATCH);
	return qtrue;
}

extern "C" void vk_sl_release_frame_resources(void)
{
	// The engine has drained GPU work. Stop feature use before untagging inputs;
	// keep the interposer alive through swapchain destruction (its hooks own it).
	vk_sl_set_frame_generation_active(qfalse);
	if (g_sl.initialized && g_sl.frameGenerationResourcesAllocated && g_sl.freeResources) {
		const sl::Result result = g_sl.freeResources(sl::kFeatureDLSS_G, sl::ViewportHandle(0));
		if (result != sl::Result::eOk)
			ri.Printf(PRINT_WARNING, "Streamline Frame Generation resource release failed (result %d)\n", (int)result);
		else {
			ri.Printf(PRINT_ALL, "Streamline Frame Generation resources released before image destruction\n");
			g_sl.frameGenerationResourcesAllocated = false;
		}
	}
	// Release SR's NGX feature while the shared device runtime is still live.
	if (g_sl.initialized && g_sl.rrAttempted && g_sl.freeResources) {
		const sl::Result result = g_sl.freeResources(sl::kFeatureDLSS_RR, sl::ViewportHandle(0));
		if (result != sl::Result::eOk && result != sl::Result::eErrorInvalidParameter)
			ri.Printf(PRINT_WARNING, "Ray Reconstruction resource release failed (%d)\n", (int)result);
		g_sl.rrAttempted = false;
	}
	// The separately initialized NR snippet must not shut it down first.
	if (g_sl.initialized && g_sl.dlssEvaluationAttempted && g_sl.freeResources) {
		const sl::Result result = g_sl.freeResources(sl::kFeatureDLSS, sl::ViewportHandle(0));
		if (result != sl::Result::eOk && result != sl::Result::eErrorInvalidParameter)
			ri.Printf(PRINT_WARNING, "Streamline DLSS resource release failed (result %d)\n", (int)result);
		else
			ri.Printf(PRINT_ALL, "Streamline DLSS resources released before Neural Rendering shutdown\n");
		g_sl.dlssEvaluationAttempted = false;
	}
	vk_dlssnr_shutdown();
	if (g_sl.initialized && g_sl.frameToken && g_sl.setTagForFrame) {
		const sl::ResourceTag tags[] = {
			sl::ResourceTag(nullptr, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent),
			sl::ResourceTag(nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent)
		};
		const sl::Result result = g_sl.setTagForFrame(*g_sl.frameToken, sl::ViewportHandle(0),
			tags, static_cast<uint32_t>(std::size(tags)), nullptr);
		if (result != sl::Result::eOk)
			ri.Printf(PRINT_WARNING, "Streamline frame resource release failed (result %d)\n", (int)result);
		else
			ri.Printf(PRINT_ALL, "Streamline frame resource tags released before image destruction\n");
	}
	g_sl.frameToken = nullptr;
	g_sl.havePreviousCamera = false;
}

extern "C" void vk_sl_shutdown(void)
{
	// Release our module references as well as the feature BEFORE Streamline
	// unloads its NGX/logging providers. DLL detach is part of the dependency
	// lifetime too; deferring it until vk_sl_unload leaves providers dead first.
	vk_dlssnr_unload();
	if (g_sl.initialized && g_sl.shutdown) {
		const int shutdownStart = ri.Milliseconds();
		ri.Printf(PRINT_ALL, "PT_SDK_SHUTDOWN_BEGIN time_ms=%d\n", shutdownStart);
		const sl::Result result = g_sl.shutdown();
		ri.Printf(PRINT_ALL, "PT_SDK_SHUTDOWN_END elapsed_ms=%d\n", ri.Milliseconds() - shutdownStart);
		if (result != sl::Result::eOk)
			ri.Printf(PRINT_WARNING, "Streamline shutdown failed (result %d)\n", (int)result);
		else
			ri.Printf(PRINT_ALL, "Streamline shutdown complete before device destruction\n");
		g_sl.initialized = false;
	}
}

extern "C" void vk_sl_unload(void)
{
	vk_dlssnr_unload();
	clearState(true);
}

extern "C" qboolean vk_sl_is_initialized(void)
{
	return g_sl.initialized ? qtrue : qfalse;
}

extern "C" uint32_t vk_sl_instance_extension_count(void)
{
	return static_cast<uint32_t>(g_sl.instanceExtensions.size());
}

extern "C" const char *vk_sl_instance_extension(uint32_t index)
{
	return index < g_sl.instanceExtensions.size() ? g_sl.instanceExtensions[index] : nullptr;
}

extern "C" uint32_t vk_sl_device_extension_count(void)
{
	return static_cast<uint32_t>(g_sl.deviceExtensions.size());
}

extern "C" const char *vk_sl_device_extension(uint32_t index)
{
	return index < g_sl.deviceExtensions.size() ? g_sl.deviceExtensions[index] : nullptr;
}

extern "C" void vk_sl_check_feature_support(VkInstance instance,
	VkPhysicalDevice physicalDevice, VkDevice device)
{
	if (!g_sl.initialized) {
		return;
	}

	sl::AdapterInfo adapter;
	adapter.vkPhysicalDevice = physicalDevice;
	g_sl.dlssSupported = g_sl.isFeatureSupported(sl::kFeatureDLSS, adapter) == sl::Result::eOk;
	g_sl.rrSupported = g_sl.rrRequirementsAvailable &&
		g_sl.isFeatureSupported(sl::kFeatureDLSS_RR, adapter) == sl::Result::eOk;
	g_sl.frameGenerationSupported =
		g_sl.isFeatureSupported(sl::kFeatureDLSS_G, adapter) == sl::Result::eOk;
	g_sl.reflexSupported = g_sl.isFeatureSupported(sl::kFeatureReflex, adapter) == sl::Result::eOk;
	const bool neuralRenderingSupported = vk_dlssnr_attach(instance, physicalDevice,
		device, g_sl.getInstanceProcAddr, g_sl.getDeviceProcAddr) == qtrue;
	ri.Cvar_Set("r_dlssAvailable", g_sl.dlssSupported ? "1" : "0");
	ri.Cvar_Set("r_dlssNeuralRenderingAvailable", neuralRenderingSupported ? "1" : "0");
	ri.Cvar_Set("r_dlssFrameGenerationAvailable", g_sl.frameGenerationSupported ? "1" : "0");
	ri.Cvar_Set("r_reflexAvailable", g_sl.reflexSupported ? "1" : "0");

	/*
	 * Resolving the feature interfaces is also the SDK's synchronization point
	 * after the Vulkan-device hook has initialized the plugins.  Do this before
	 * creating the Win32 surface so DLSS-G can associate it with the SDL HWND.
	 */
	void *function = nullptr;
	if (g_sl.rrSupported) {
		if (g_sl.getFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDSetOptions", function) == sl::Result::eOk)
			g_sl.rrSetOptions = reinterpret_cast<PFun_slDLSSDSetOptions *>(function);
		function = nullptr;
		if (g_sl.getFeatureFunction(sl::kFeatureDLSS_RR, "slDLSSDGetOptimalSettings", function) == sl::Result::eOk)
			g_sl.rrGetOptimalSettings = reinterpret_cast<PFun_slDLSSDGetOptimalSettings *>(function);
		g_sl.rrSupported = g_sl.rrSetOptions && g_sl.rrGetOptimalSettings;
		function = nullptr;
	}
	ri.Cvar_Set("r_dlssRayReconstructionAvailable", g_sl.rrSupported ? "1" : "0");
	if (g_sl.dlssSupported &&
		g_sl.getFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", function) == sl::Result::eOk) {
		g_sl.dlssSetOptions = reinterpret_cast<PFun_slDLSSSetOptions *>(function);
		function = nullptr;
		if (g_sl.getFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", function) == sl::Result::eOk)
			g_sl.dlssGetOptimalSettings = reinterpret_cast<PFun_slDLSSGetOptimalSettings *>(function);
	}
	function = nullptr;
	if (g_sl.frameGenerationSupported &&
		g_sl.getFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGSetOptions", function) == sl::Result::eOk)
		g_sl.dlssGSetOptions = reinterpret_cast<PFun_slDLSSGSetOptions *>(function);
	function = nullptr;
	if (g_sl.frameGenerationSupported &&
		g_sl.getFeatureFunction(sl::kFeatureDLSS_G, "slDLSSGGetState", function) == sl::Result::eOk)
		g_sl.dlssGGetState = reinterpret_cast<PFun_slDLSSGGetState *>(function);
	function = nullptr;
	if (g_sl.reflexSupported &&
		g_sl.getFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions", function) == sl::Result::eOk)
		g_sl.reflexSetOptions = reinterpret_cast<PFun_slReflexSetOptions *>(function);
	function = nullptr;
	if (g_sl.reflexSupported &&
		g_sl.getFeatureFunction(sl::kFeatureReflex, "slReflexSleep", function) == sl::Result::eOk)
		g_sl.reflexSleep = reinterpret_cast<PFun_slReflexSleep *>(function);
	function = nullptr;
	if (g_sl.getFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", function) == sl::Result::eOk)
		g_sl.pclSetMarker = reinterpret_cast<PFun_slPCLSetMarker *>(function);
	unloadIdleFrameGenerationHooks();

	g_sl.status = g_sl.dlssSupported
		? "DLSS Super Resolution is supported"
		: "DLSS Super Resolution is unavailable on this GPU/driver";
	ri.Printf(PRINT_ALL, "NVIDIA Streamline: DLSS SR %s, Neural Rendering %s, Frame Generation %s, Reflex %s\n",
		g_sl.dlssSupported ? "supported" : "unavailable",
		neuralRenderingSupported ? "available" : "unavailable",
		g_sl.frameGenerationSupported ? "supported" : "unavailable",
		g_sl.reflexSupported ? "supported" : "unavailable");
}

extern "C" void *vk_sl_get_instance_proc_addr(VkInstance instance, const char *name)
{
	if (g_sl.getInstanceProcAddr && name && !strcmp(name, "vkGetInstanceProcAddr")) {
		return reinterpret_cast<void *>(g_sl.getInstanceProcAddr);
	}
	/*
	 * Streamline's vkGetInstanceProcAddr intentionally does not intercept the
	 * Win32 surface entry points.  NVIDIA's Vulkan sample links those exported
	 * interposer functions directly; because this renderer loads Streamline at
	 * runtime, resolve the equivalent exports from sl.interposer.dll here.
	 * DLSS-G uses these hooks to associate the VkSurfaceKHR with its HWND.
	 */
	if (g_sl.module && name &&
		(!strcmp(name, "vkCreateWin32SurfaceKHR") ||
		 !strcmp(name, "vkDestroySurfaceKHR"))) {
		if (FARPROC function = GetProcAddress(g_sl.module, name))
			return reinterpret_cast<void *>(function);
	}
	return g_sl.getInstanceProcAddr ?
		reinterpret_cast<void *>(g_sl.getInstanceProcAddr(instance, name)) : nullptr;
}

extern "C" void *vk_sl_get_device_proc_addr(VkDevice device, const char *name)
{
	return g_sl.getDeviceProcAddr ?
		reinterpret_cast<void *>(g_sl.getDeviceProcAddr(device, name)) : nullptr;
}

extern "C" qboolean vk_sl_dlss_supported(void)
{
	return g_sl.dlssSupported ? qtrue : qfalse;
}

extern "C" qboolean vk_sl_ray_reconstruction_enabled(void)
{
	return g_sl.rrEnabled ? qtrue : qfalse;
}

extern "C" qboolean vk_sl_neural_rendering_supported(void)
{
	return vk_dlssnr_supported();
}

extern "C" qboolean vk_sl_frame_generation_supported(void)
{
	return g_sl.frameGenerationSupported ? qtrue : qfalse;
}

extern "C" void vk_sl_set_frame_generation_presentation_supported(qboolean supported)
{
	g_sl.frameGenerationPresentationSupported = supported != qfalse;
	ri.Cvar_Set("r_dlssFrameGenerationAvailable",
		g_sl.frameGenerationSupported && supported ? "1" : "0");
	if (g_sl.frameGenerationSupported && !supported)
		ri.Printf(PRINT_WARNING, "Frame Generation unavailable: this surface has no unsynchronized presentation mode\n");
}

extern "C" qboolean vk_sl_reflex_supported(void)
{
	return g_sl.reflexSupported ? qtrue : qfalse;
}

extern "C" const char *vk_sl_status(void)
{
	return g_sl.status.c_str();
}

extern "C" void vk_sl_info_f(void)
{
	ri.Printf(PRINT_ALL, "Ray Reconstruction: supported %d, enabled %d, evaluated frames %u\n",
		g_sl.rrSupported, g_sl.rrEnabled, g_sl.rrFrames);
	// Report cached results; querying here would consume presentation counters
	// and interfere with the next frame's normal SDK status polling.
	ri.Printf(PRINT_ALL, "NVIDIA: initialized %d, DLSS enabled %d, NR available %d\n",
		g_sl.initialized, g_sl.dlssEnabled, vk_dlssnr_supported());
	ri.Printf(PRINT_ALL, "NVIDIA idle FG hooks: unloaded %d, FG capability retained %d\n",
		g_sl.frameGenerationHooksUnloaded, g_sl.frameGenerationSupported);
	ri.Printf(PRINT_ALL, "Frame Generation: enabled %d, viewport active %d, queries %u, presented %u, peak %u, result %d, status 0x%x\n",
		g_sl.frameGenerationEnabled, g_sl.frameGenerationActive, g_sl.frameGenerationQueries,
		g_sl.frameGenerationPresented, g_sl.frameGenerationPeakPresented,
		(int)g_sl.frameGenerationResult, (unsigned)g_sl.frameGenerationStatus);
	ri.Printf(PRINT_ALL, "NVIDIA frame history: %u constant frames, %u resets\n",
		g_sl.constantFrames, g_sl.constantResets);
	ri.Printf(PRINT_ALL, "NVIDIA window state: focused %d, minimized %d\n",
		!ri.Cvar_VariableIntegerValue("com_unfocused"),
		ri.Cvar_VariableIntegerValue("com_minimized"));
	ri.Printf(PRINT_ALL, "NVIDIA FG query history: unfocused %u, minimized %u, failed %u\n",
		g_sl.frameGenerationUnfocusedQueries, g_sl.frameGenerationMinimizedQueries,
		g_sl.frameGenerationFailedQueries);
}

extern "C" qboolean vk_sl_configure(int dlssMode, int frameGeneration, int reflexMode,
	uint32_t outputWidth, uint32_t outputHeight,
	uint32_t *renderWidth, uint32_t *renderHeight, float ptScaleIn)
{
	if (renderWidth) *renderWidth = outputWidth;
	if (renderHeight) *renderHeight = outputHeight;
	if (!g_sl.initialized)
		return qfalse;

	// When path tracing runs at a fractional render scale the DLSS/RR plugins
	// are bound to a fixed input-to-output ratio per mode (an eDLAA input must
	// equal the output, otherwise NGX evaluate fails with 0xbad00005). Select
	// the quality preset whose ratio matches the requested scale and, when the
	// scale matches no preset exactly, disable runtime evaluation entirely so
	// the final present blit performs the upscale instead.
	g_sl.ptScale = ptScaleIn;
	if (g_sl.ptScale >= 1.0f) {
		g_sl.ptScaleModeMatched = false;
	} else if (g_sl.dlssSetOptions) {
		static const float presetRatios[] = { 0.667f, 0.5f, 0.333f, 0.25f };
		float best = 1e9f;
		int bestIndex = 1;
		for (int i = 0; i < 4; ++i) {
			const float d = fabsf(g_sl.ptScale - presetRatios[i]);
			if (d < best) {
				best = d;
				bestIndex = i;
			}
		}
		g_sl.ptScaleModeMatched = best < 0.05f;
		if (g_sl.ptScaleModeMatched)
			dlssMode = bestIndex + 1; // eMaxQuality..eUltraPerformance
	}

	static const sl::DLSSMode modes[] = {
		sl::DLSSMode::eOff,
		sl::DLSSMode::eMaxQuality,
		sl::DLSSMode::eBalanced,
		sl::DLSSMode::eMaxPerformance,
		sl::DLSSMode::eUltraPerformance,
		sl::DLSSMode::eDLAA
	};
	dlssMode = std::max(0, std::min(dlssMode, 5));
	g_sl.dlssEnabled = dlssMode != 0 && g_sl.dlssSupported;
	g_sl.rrEnabled = g_sl.dlssEnabled && g_sl.rrSupported &&
		ri.Cvar_VariableIntegerValue("r_rayTracing") == 2 &&
		ri.Cvar_VariableIntegerValue("r_dlssRayReconstruction") != 0 &&
		(g_sl.ptScale >= 1.0f || g_sl.ptScaleModeMatched);
	frameGeneration = frameGeneration && g_sl.frameGenerationSupported &&
		g_sl.frameGenerationPresentationSupported;
	g_sl.frameGenerationEnabled = frameGeneration != 0;
	g_sl.frameGenerationActive = false;
	g_sl.havePreviousCamera = false;

	if (g_sl.dlssSetOptions) {
		sl::DLSSOptions options;
		options.mode = g_sl.dlssSupported ? modes[dlssMode] : sl::DLSSMode::eOff;
		options.outputWidth = outputWidth;
		options.outputHeight = outputHeight;
		options.colorBuffersHDR = sl::Boolean::eFalse;
		options.useAutoExposure = sl::Boolean::eTrue;
		if (g_sl.dlssSetOptions(sl::ViewportHandle(0), options) != sl::Result::eOk) {
			g_sl.status = "DLSS rejected the selected rendering mode";
			return qfalse;
		}
		if (dlssMode && g_sl.dlssGetOptimalSettings && g_sl.ptScale >= 1.0f) {
			sl::DLSSOptimalSettings settings;
			if (g_sl.dlssGetOptimalSettings(options, settings) == sl::Result::eOk &&
				settings.optimalRenderWidth && settings.optimalRenderHeight) {
				if (renderWidth) *renderWidth = settings.optimalRenderWidth;
				if (renderHeight) *renderHeight = settings.optimalRenderHeight;
			}
		}
	}

	if (g_sl.rrEnabled) {
		g_sl.rrOptions.mode = modes[dlssMode];
		g_sl.rrOptions.outputWidth = outputWidth;
		g_sl.rrOptions.outputHeight = outputHeight;
		g_sl.rrOptions.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
		g_sl.rrOptions.colorBuffersHDR = sl::Boolean::eTrue;
		sl::DLSSDOptimalSettings settings;
		if (g_sl.ptScale >= 1.0f) {
			if (g_sl.rrGetOptimalSettings(g_sl.rrOptions, settings) == sl::Result::eOk &&
				settings.optimalRenderWidth && settings.optimalRenderHeight) {
				if (renderWidth) *renderWidth = settings.optimalRenderWidth;
				if (renderHeight) *renderHeight = settings.optimalRenderHeight;
			} else {
				g_sl.rrEnabled = false;
				ri.Printf(PRINT_WARNING, "Ray Reconstruction rejected this mode; native reconstruction retained\n");
			}
		}
	}

	if (g_sl.reflexSetOptions) {
		sl::ReflexOptions options;
		reflexMode = std::max(0, std::min(reflexMode, 2));
		options.mode = static_cast<sl::ReflexMode>(reflexMode);
		/* Frame Generation requires Reflex; enforce the dependency here too. */
		if (frameGeneration && options.mode == sl::ReflexMode::eOff)
			options.mode = sl::ReflexMode::eLowLatency;
		g_sl.reflexSetOptions(options);
	}

	if (g_sl.dlssGSetOptions) {
		sl::DLSSGOptions options;
		/* Enable only after a gameplay frame has supplied valid constants/tags. */
		options.mode = sl::DLSSGMode::eOff;
		options.numFramesToGenerate = 1;
		options.mvecDepthWidth = renderWidth ? *renderWidth : outputWidth;
		options.mvecDepthHeight = renderHeight ? *renderHeight : outputHeight;
		options.colorWidth = outputWidth;
		options.colorHeight = outputHeight;
		g_sl.dlssGOptions = options;
		g_sl.frameGenerationStateLogged = false;
		g_sl.dlssGSetOptions(sl::ViewportHandle(0), g_sl.dlssGOptions);
	}

	ri.Printf(PRINT_ALL, "NVIDIA DLSS mode %d: render %ux%u -> output %ux%u, Frame Generation %s, Reflex %d\n",
		dlssMode, renderWidth ? *renderWidth : outputWidth,
		renderHeight ? *renderHeight : outputHeight, outputWidth, outputHeight,
		frameGeneration ? "on" : "off", reflexMode);
	return qtrue;
}

extern "C" qboolean vk_sl_begin_frame(void)
{
	if (!g_sl.initialized || !g_sl.getNewFrameToken)
		return qfalse;
	/* DLSS-G presents asynchronously.  Query the completed previous present at
	 * the start of the next application frame, as required by NVIDIA's sample. */
	queryFrameGenerationState();
	if (g_sl.getNewFrameToken(g_sl.frameToken, nullptr) != sl::Result::eOk || !g_sl.frameToken)
		return qfalse;
	if (g_sl.reflexSleep)
		g_sl.reflexSleep(*g_sl.frameToken);
	if (g_sl.pclSetMarker)
		g_sl.pclSetMarker(sl::PCLMarker::eSimulationStart, *g_sl.frameToken);
	return qtrue;
}

static qboolean evaluateReconstruction(const vk_sl_frame_resources_t *r, bool rr)
{
	if (!r || !g_sl.frameToken || !g_sl.setConstants ||
		!g_sl.setTagForFrame || !g_sl.evaluateFeature)
		return qfalse;
	// A path tracing scale that matches no NVIDIA DLSS quality preset cannot
	// be evaluated; the final present blit upscales the raw render instead.
	if (g_sl.ptScale < 1.0f && !g_sl.ptScaleModeMatched)
		return qfalse;

	sl::Constants constants;
	copyColumnMajorToRowMajor(constants.cameraViewToClip, r->projection_matrix);
	sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);
	setIdentity(constants.clipToLensClip);

	sl::float4x4 worldToCamera;
	copyColumnMajorToRowMajor(worldToCamera, r->view_matrix);
	sl::float4x4 cameraToWorld;
	sl::matrixFullInvert(cameraToWorld, worldToCamera);
	if (rr) {
		g_sl.rrOptions.worldToCameraView = worldToCamera;
		g_sl.rrOptions.cameraViewToWorld = cameraToWorld;
		g_sl.rrOptions.preExposure = 1.0f;
		g_sl.rrOptions.exposureScale = r->exposure;
		if (g_sl.rrSetOptions(sl::ViewportHandle(0), g_sl.rrOptions) != sl::Result::eOk)
			return qfalse;
	}
	if (g_sl.havePreviousCamera && !r->reset) {
		sl::float4x4 cameraToPrevious;
		sl::float4x4 clipToPreviousCamera;
		sl::calcCameraToPrevCamera(cameraToPrevious, cameraToWorld, g_sl.previousCameraToWorld);
		sl::matrixMul(clipToPreviousCamera, constants.clipToCameraView, cameraToPrevious);
		sl::matrixMul(constants.clipToPrevClip, clipToPreviousCamera, g_sl.previousProjection);
		sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);
	} else {
		setIdentity(constants.clipToPrevClip);
		setIdentity(constants.prevClipToClip);
	}

	// The renderer adds +2*jitter/extent to projection[8:9]. With clip.w=-z
	// this moves the image by -jitter pixels (and the primary ray sample by
	// +jitter). NGX expects the IMAGE displacement, not the ray-sample offset.
	// Passing +jitter made reconstruction double the visible Halton wobble.
	constants.jitterOffset = sl::float2(-r->jitter_x, -r->jitter_y);
	constants.mvecScale = sl::float2(1.0f, 1.0f);
	constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
	constants.cameraPos = sl::float3(r->camera_origin[0], r->camera_origin[1], r->camera_origin[2]);
	constants.cameraFwd = sl::float3(r->camera_axis[0], r->camera_axis[1], r->camera_axis[2]);
	constants.cameraRight = sl::float3(-r->camera_axis[3], -r->camera_axis[4], -r->camera_axis[5]);
	constants.cameraUp = sl::float3(r->camera_axis[6], r->camera_axis[7], r->camera_axis[8]);
	constants.cameraNear = r->camera_near;
	constants.cameraFar = r->camera_far;
	constants.cameraFOV = r->camera_fov_y;
	constants.cameraAspectRatio = r->camera_aspect;
	constants.motionVectorsInvalidValue = 0.0f;
	constants.depthInverted = sl::Boolean::eFalse;
	constants.cameraMotionIncluded = r->camera_motion_included ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	constants.motionVectors3D = sl::Boolean::eFalse;
	constants.reset = (!g_sl.havePreviousCamera || r->reset) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	constants.motionVectorsJittered = sl::Boolean::eFalse;
	++g_sl.constantFrames;
	if (constants.reset == sl::Boolean::eTrue) ++g_sl.constantResets;

	const sl::ViewportHandle viewport(0);
	if (g_sl.setConstants(constants, *g_sl.frameToken, viewport) != sl::Result::eOk)
		return qfalse;

	const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags depthUsage = (r->camera_motion_included ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags motionUsage = VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	sl::Resource depth(sl::ResourceType::eTex2d, nativeHandle(r->depth), nullptr,
		nativeHandle(r->depth_view), static_cast<uint32_t>(r->depth_layout));
	sl::Resource motion(sl::ResourceType::eTex2d, nativeHandle(r->motion_vectors), nullptr,
		nativeHandle(r->motion_vectors_view), static_cast<uint32_t>(r->motion_vectors_layout));
	describeResource(depth, r->render_width, r->render_height, r->depth_format, depthUsage);
	describeResource(motion, r->render_width, r->render_height, r->motion_vectors_format, motionUsage);

	// Resource dimensions describe the allocation; tags also describe the
	// active viewport. Both reconstruction guides cover the full render extent.
	const sl::Extent renderExtent{0, 0, r->render_width, r->render_height};
	const sl::ResourceTag commonTags[] = {
		sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent)
	};
	sl::CommandBuffer *commandBuffer = reinterpret_cast<sl::CommandBuffer *>(r->command_buffer);
	if (g_sl.setTagForFrame(*g_sl.frameToken, viewport, commonTags,
		static_cast<uint32_t>(std::size(commonTags)), commandBuffer) != sl::Result::eOk)
		return qfalse;

	g_sl.previousCameraToWorld = cameraToWorld;
	g_sl.previousProjection = constants.cameraViewToClip;
	g_sl.havePreviousCamera = true;
	if (!g_sl.dlssEnabled)
		return qfalse;

	sl::Resource colorIn(sl::ResourceType::eTex2d, nativeHandle(r->color_input), nullptr,
		nativeHandle(r->color_input_view), static_cast<uint32_t>(r->color_input_layout));
	sl::Resource colorOut(sl::ResourceType::eTex2d, nativeHandle(r->color_output), nullptr,
		nativeHandle(r->color_output_view), static_cast<uint32_t>(r->color_output_layout));
	describeResource(colorIn, r->render_width, r->render_height, r->color_format, colorUsage);
	describeResource(colorOut, r->output_width, r->output_height, r->color_format, colorUsage);
	const sl::ResourceTag scalingTags[] = {
		sl::ResourceTag(&colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent),
		sl::ResourceTag(&colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent)
	};
	if (g_sl.setTagForFrame(*g_sl.frameToken, viewport, scalingTags,
		static_cast<uint32_t>(std::size(scalingTags)), commandBuffer) != sl::Result::eOk)
		return qfalse;
	const sl::BaseStructure *inputs[] = { &viewport };
	if (rr) {
		sl::Resource guides[4];
		sl::ResourceTag tags[4];
		const sl::BufferType types[] = { sl::kBufferTypeAlbedo, sl::kBufferTypeSpecularAlbedo,
			sl::kBufferTypeNormalRoughness, sl::kBufferTypeSpecularHitDistance };
		for (unsigned i = 0; i < 4; ++i) {
			guides[i] = sl::Resource(sl::ResourceType::eTex2d, nativeHandle(r->rr_guides[i]), nullptr,
				nativeHandle(r->rr_guide_views[i]), VK_IMAGE_LAYOUT_GENERAL);
			describeResource(guides[i], r->render_width, r->render_height,
				i == 3 ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT, colorUsage);
			tags[i] = sl::ResourceTag(&guides[i], types[i], sl::ResourceLifecycle::eValidUntilPresent, &renderExtent);
		}
		if (g_sl.setTagForFrame(*g_sl.frameToken, viewport, tags, 4, commandBuffer) != sl::Result::eOk)
			return qfalse;
		g_sl.rrAttempted = true;
		const sl::Result result = g_sl.evaluateFeature(sl::kFeatureDLSS_RR, *g_sl.frameToken, inputs, 1, commandBuffer);
		if (result != sl::Result::eOk) {
			ri.Printf(PRINT_WARNING, "Ray Reconstruction evaluation failed (%d); native reconstruction retained\n", (int)result);
			return qfalse;
		}
		if (!g_sl.rrFrames++) ri.Printf(PRINT_ALL, "NVIDIA DLSS Ray Reconstruction active: %ux%u -> %ux%u\n",
			r->render_width, r->render_height, r->output_width, r->output_height);
		return qtrue;
	}
	g_sl.dlssEvaluationAttempted = true;
	const bool ok = g_sl.evaluateFeature(sl::kFeatureDLSS, *g_sl.frameToken, inputs,
		static_cast<uint32_t>(std::size(inputs)), commandBuffer) == sl::Result::eOk;
	return ok ? qtrue : qfalse;
}

extern "C" qboolean vk_sl_evaluate_dlss(const vk_sl_frame_resources_t *r)
{
	if (g_sl.rrAttempted && g_sl.rrSetOptions) {
		sl::DLSSDOptions off = g_sl.rrOptions;
		off.mode = sl::DLSSMode::eOff;
		g_sl.rrSetOptions(sl::ViewportHandle(0), off);
	}
	return evaluateReconstruction(r, false);
}

extern "C" qboolean vk_sl_evaluate_ray_reconstruction(const vk_sl_frame_resources_t *r)
{
	if (!g_sl.rrEnabled) return qfalse;
	if (evaluateReconstruction(r, true)) return qtrue;
	g_sl.rrEnabled = false; // No repeated failing initialization/evaluation every frame.
	g_sl.havePreviousCamera = false;
	sl::DLSSDOptions off = g_sl.rrOptions;
	off.mode = sl::DLSSMode::eOff;
	g_sl.rrSetOptions(sl::ViewportHandle(0), off);
	return qfalse;
}

extern "C" void vk_sl_configure_neural_rendering(int mode, uint32_t width,
	uint32_t height)
{
	vk_dlssnr_configure(g_sl.rrEnabled ? 0 : mode, width, height);
}

extern "C" qboolean vk_sl_prepare_neural_rendering(VkCommandBuffer commandBuffer)
{
	return vk_dlssnr_prepare(commandBuffer);
}

extern "C" qboolean vk_sl_evaluate_neural_rendering(
	const vk_sl_frame_resources_t *resources)
{
	return vk_dlssnr_evaluate(resources);
}

extern "C" void vk_sl_tag_hudless(VkCommandBuffer commandBuffer, VkImage image,
	VkImageView view, VkFormat format, VkImageLayout layout,
	uint32_t width, uint32_t height, VkImageUsageFlags usage)
{
	if (!g_sl.frameGenerationEnabled || !g_sl.frameToken || !g_sl.setTagForFrame)
		return;
	sl::Resource color(sl::ResourceType::eTex2d, nativeHandle(image), nullptr,
		nativeHandle(view), static_cast<uint32_t>(layout));
	describeResource(color, width, height, format, usage);
	const sl::Extent fullExtent{0, 0, width, height};
	const sl::ResourceTag tag(&color, sl::kBufferTypeHUDLessColor,
		sl::ResourceLifecycle::eValidUntilPresent, &fullExtent);
	const sl::ViewportHandle viewport(0);
	g_sl.setTagForFrame(*g_sl.frameToken, viewport, &tag, 1,
		reinterpret_cast<sl::CommandBuffer *>(commandBuffer));
}

extern "C" void vk_sl_tag_backbuffer_extent(VkCommandBuffer commandBuffer,
	uint32_t width, uint32_t height)
{
	if (!g_sl.frameGenerationEnabled || !g_sl.frameToken || !g_sl.setTagForFrame)
		return;
	const sl::Extent fullExtent{0, 0, width, height};
	/* Streamline already owns the presented backbuffer resource.  A null
	 * resource plus extent is NVIDIA's documented full-viewport tag. */
	const sl::ResourceTag tag(nullptr, sl::kBufferTypeBackbuffer,
		sl::ResourceLifecycle::eValidUntilPresent, &fullExtent);
	const sl::ViewportHandle viewport(0);
	g_sl.setTagForFrame(*g_sl.frameToken, viewport, &tag, 1,
		reinterpret_cast<sl::CommandBuffer *>(commandBuffer));
}

extern "C" void vk_sl_set_frame_generation_active(qboolean active)
{
	const bool enable = active && g_sl.frameGenerationEnabled;
	if (!g_sl.dlssGSetOptions || enable == g_sl.frameGenerationActive)
		return;
	g_sl.frameGenerationActive = enable;
	g_sl.dlssGOptions.mode = enable ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	g_sl.frameGenerationStateLogged = false;
	const sl::Result result = g_sl.dlssGSetOptions(sl::ViewportHandle(0), g_sl.dlssGOptions);
	if (result != sl::Result::eOk) {
		ri.Printf(PRINT_WARNING, "NVIDIA DLSS Frame Generation could not be %s (result %d)\n",
			enable ? "enabled" : "disabled", (int)result);
		g_sl.frameGenerationActive = false;
	} else if (enable) {
		g_sl.frameGenerationResourcesAllocated = true;
	}
}

extern "C" void vk_sl_mark_render_submit(qboolean start)
{
	if (!g_sl.frameToken || !g_sl.pclSetMarker)
		return;
	if (start) {
		g_sl.pclSetMarker(sl::PCLMarker::eSimulationEnd, *g_sl.frameToken);
		g_sl.pclSetMarker(sl::PCLMarker::eRenderSubmitStart, *g_sl.frameToken);
	} else {
		g_sl.pclSetMarker(sl::PCLMarker::eRenderSubmitEnd, *g_sl.frameToken);
	}
}

extern "C" void vk_sl_mark_present(qboolean start)
{
	if (g_sl.frameToken && g_sl.pclSetMarker)
		g_sl.pclSetMarker(start ? sl::PCLMarker::ePresentStart : sl::PCLMarker::ePresentEnd,
			*g_sl.frameToken);
}

#else

extern "C" qboolean vk_sl_initialize(void) { return qfalse; }
extern "C" qboolean vk_sl_verify_nvidia_signature(const wchar_t *) { return qfalse; }
extern "C" void vk_sl_release_frame_resources(void) {}
extern "C" void vk_sl_shutdown(void) {}
extern "C" void vk_sl_unload(void) {}
extern "C" void vk_sl_info_f(void) { ri.Printf(PRINT_ALL, "NVIDIA support is not compiled into this renderer\n"); }
extern "C" void vk_sl_set_frame_generation_presentation_supported(qboolean) {}
extern "C" qboolean vk_sl_is_initialized(void) { return qfalse; }
extern "C" uint32_t vk_sl_instance_extension_count(void) { return 0; }
extern "C" const char *vk_sl_instance_extension(uint32_t) { return nullptr; }
extern "C" uint32_t vk_sl_device_extension_count(void) { return 0; }
extern "C" const char *vk_sl_device_extension(uint32_t) { return nullptr; }
extern "C" void vk_sl_check_feature_support(VkInstance, VkPhysicalDevice, VkDevice) {}
extern "C" void *vk_sl_get_instance_proc_addr(VkInstance, const char *) { return nullptr; }
extern "C" void *vk_sl_get_device_proc_addr(VkDevice, const char *) { return nullptr; }
extern "C" qboolean vk_sl_dlss_supported(void) { return qfalse; }
extern "C" qboolean vk_sl_ray_reconstruction_enabled(void) { return qfalse; }
extern "C" qboolean vk_sl_neural_rendering_supported(void) { return qfalse; }
extern "C" qboolean vk_sl_frame_generation_supported(void) { return qfalse; }
extern "C" qboolean vk_sl_reflex_supported(void) { return qfalse; }
extern "C" const char *vk_sl_status(void) { return "NVIDIA Streamline was not included in this build"; }
extern "C" qboolean vk_sl_configure(int, int, int, uint32_t w, uint32_t h, uint32_t *rw, uint32_t *rh) {
	if (rw) *rw = w; if (rh) *rh = h; return qfalse;
}
extern "C" qboolean vk_sl_begin_frame(void) { return qfalse; }
extern "C" qboolean vk_sl_evaluate_dlss(const vk_sl_frame_resources_t *) { return qfalse; }
extern "C" qboolean vk_sl_evaluate_ray_reconstruction(const vk_sl_frame_resources_t *) { return qfalse; }
extern "C" void vk_sl_configure_neural_rendering(int, uint32_t, uint32_t) {}
extern "C" qboolean vk_sl_prepare_neural_rendering(VkCommandBuffer) { return qfalse; }
extern "C" qboolean vk_sl_evaluate_neural_rendering(const vk_sl_frame_resources_t *) { return qfalse; }
extern "C" void vk_sl_tag_hudless(VkCommandBuffer, VkImage, VkImageView, VkFormat, VkImageLayout, uint32_t, uint32_t, VkImageUsageFlags) {}
extern "C" void vk_sl_tag_backbuffer_extent(VkCommandBuffer, uint32_t, uint32_t) {}
extern "C" void vk_sl_set_frame_generation_active(qboolean) {}
extern "C" void vk_sl_mark_render_submit(qboolean) {}
extern "C" void vk_sl_mark_present(qboolean) {}

#endif
