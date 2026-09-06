/*
 * DLSS Neural Rendering's current snippet runtime validates that its Vulkan
 * entry points are called by a module named nvngx.dll.  Keep these forwarding
 * calls in a separately compiled, non-tail-called module so the return address
 * seen by the signed NVIDIA DLL belongs to that module.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

typedef uint32_t (__cdecl *ngx_init_vk_ext2_t)(uint64_t, const wchar_t *,
	void *, void *, void *, void *, void *, uint32_t, const void *);
typedef uint32_t (__cdecl *ngx_populate_t)(void *);
typedef uint32_t (__cdecl *ngx_create_t)(void *, int, void *, void **);
typedef uint32_t (__cdecl *ngx_evaluate_t)(void *, const void *, const void *, void *);
typedef uint32_t (__cdecl *ngx_release_t)(void *);

#if defined(__GNUC__)
#define BRIDGE_NOINLINE __attribute__((noinline, optimize("O0")))
#else
#define BRIDGE_NOINLINE __declspec(noinline)
#endif

extern "C" __declspec(dllexport) BRIDGE_NOINLINE uint32_t __cdecl
NVNGXBridge_VULKAN_InitExt2(ngx_init_vk_ext2_t init, uint64_t application_id,
	const wchar_t *application_data_path, void *instance, void *physical_device,
	void *device, void *get_instance_proc_addr, void *get_device_proc_addr,
	uint32_t api_version, const void *parameters)
{
	if (!init) return 0xBAD00005UL;
	volatile uint32_t result = init(application_id, application_data_path,
		instance, physical_device, device, get_instance_proc_addr,
		get_device_proc_addr, api_version, parameters);
	MemoryBarrier();
	return result;
}

extern "C" __declspec(dllexport) BRIDGE_NOINLINE uint32_t __cdecl
NVNGXBridge_VULKAN_PopulateParameters(ngx_populate_t populate, void *parameters)
{
	if (!populate || !parameters) return 0xBAD00005UL;
	volatile uint32_t result = populate(parameters);
	MemoryBarrier();
	return result;
}

extern "C" __declspec(dllexport) BRIDGE_NOINLINE uint32_t __cdecl
NVNGXBridge_VULKAN_CreateFeature(ngx_create_t create, void *command_buffer,
	int feature, void *parameters, void **handle)
{
	if (!create) return 0xBAD00005UL;
	volatile uint32_t result = create(command_buffer, feature, parameters, handle);
	MemoryBarrier();
	return result;
}

extern "C" __declspec(dllexport) BRIDGE_NOINLINE uint32_t __cdecl
NVNGXBridge_VULKAN_EvaluateFeature(ngx_evaluate_t evaluate, void *command_buffer,
	const void *handle, const void *parameters, void *progress_callback)
{
	if (!evaluate) return 0xBAD00005UL;
	volatile uint32_t result = evaluate(command_buffer, handle, parameters,
		progress_callback);
	MemoryBarrier();
	return result;
}

extern "C" __declspec(dllexport) BRIDGE_NOINLINE uint32_t __cdecl
NVNGXBridge_VULKAN_ReleaseFeature(ngx_release_t release, void *handle)
{
	if (!release || !handle) return 0xBAD00005UL;
	volatile uint32_t result = release(handle);
	MemoryBarrier();
	return result;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
	return TRUE;
}
