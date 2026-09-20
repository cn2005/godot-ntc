#include "ntc_runtime.h"
#include "ntc_vk.h"

#include <libntc/ntc.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/error_macros.hpp>

using namespace godot;

NtcRuntime *NtcRuntime::singleton = nullptr;

NtcRuntime *NtcRuntime::get_singleton() {
	return singleton;
}

void NtcRuntime::set_singleton(NtcRuntime *runtime) {
	singleton = runtime;
}

void NtcRuntime::_bind_methods() {
	ClassDB::bind_method(D_METHOD("initialize"), &NtcRuntime::initialize);
	ClassDB::bind_method(D_METHOD("ensure_library_loaded"), &NtcRuntime::ensure_library_loaded);
	ClassDB::bind_method(D_METHOD("shutdown"), &NtcRuntime::shutdown);
	ClassDB::bind_method(D_METHOD("is_available"), &NtcRuntime::is_available);
	ClassDB::bind_method(D_METHOD("get_capability_tier"), &NtcRuntime::get_capability_tier);
	ClassDB::bind_method(D_METHOD("get_last_error"), &NtcRuntime::get_last_error);
	ClassDB::bind_method(D_METHOD("get_library_version"), &NtcRuntime::get_library_version);
	ClassDB::bind_method(D_METHOD("get_gpu_name"), &NtcRuntime::get_gpu_name);
	ClassDB::bind_method(D_METHOD("get_coop_vec_enabled"), &NtcRuntime::get_coop_vec_enabled);
}

NtcRuntime::NtcRuntime() = default;

NtcRuntime::~NtcRuntime() {
	shutdown();
}

namespace {

bool try_load_ntc(const std::string &full, std::string &out_error) {
	if (GetModuleHandleA("libntc.dll")) {
		return true;
	}
	if (LoadLibraryA(full.c_str())) {
		return true;
	}
	out_error = "LoadLibrary(" + full + ") failed, GetLastError=" + std::to_string(GetLastError());
	return false;
}

HMODULE current_extension_module() {
	HMODULE self = nullptr;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&current_extension_module), &self)) {
		return self;
	}
	self = GetModuleHandleA("libntc_godot.dll");
	if (self) {
		return self;
	}
	return GetModuleHandleA("~libntc_godot.dll");
}

} // namespace

bool NtcRuntime::load_libntc_dll(std::string &out_error) {
	if (GetModuleHandleA("libntc.dll")) {
		return true;
	}

	std::string last;
	const HMODULE self = current_extension_module();
	char path[MAX_PATH]{};
	if (self && GetModuleFileNameA(self, path, MAX_PATH)) {
		std::string dir(path);
		const size_t slash = dir.find_last_of("\\/");
		if (slash != std::string::npos) {
			dir.resize(slash);
		}
		SetDllDirectoryA(dir.c_str());
		if (try_load_ntc(dir + "\\libntc.dll", last)) {
			return true;
		}
	}

	if (ProjectSettings *ps = ProjectSettings::get_singleton()) {
		const String res_dll = ps->globalize_path("res://bin/windows/libntc.dll");
		const CharString utf8 = res_dll.utf8();
		if (try_load_ntc(utf8.get_data(), last)) {
			return true;
		}
	}

	if (LoadLibraryA("libntc.dll")) {
		return true;
	}
	out_error = last.empty() ? "libntc.dll not found next to the GDExtension or at res://bin/windows/libntc.dll."
							 : last;
	return false;
}

bool NtcRuntime::ensure_library_loaded() {
	std::string err;
	if (!load_libntc_dll(err)) {
		last_error = String(err.c_str());
		return false;
	}
	return true;
}

bool NtcRuntime::initialize() {
	if (initialized) {
		return available;
	}
	initialized = true;

	if (!ensure_library_loaded()) {
		return false;
	}

	std::string err;
	vk = std::make_unique<NtcVkDevice>();
	if (!vk->initialize(err)) {
		last_error = String(err.c_str());
		vk.reset();
		return false;
	}

	ntc::ContextParameters params;
	params.cudaDevice = ntc::DisableCudaDevice;
	params.graphicsApi = ntc::GraphicsAPI::Vulkan;
	params.vkInstance = vk->instance;
	params.vkPhysicalDevice = vk->physical_device;
	params.vkDevice = vk->device;
	params.enableCooperativeVector = false;

	ntc::Status st = ntc::CreateContext(&context, params);
	if (st != ntc::Status::Ok && st != ntc::Status::CudaUnavailable) {
		last_error = String("CreateContext failed: ") + ntc::StatusToString(st) + " " + ntc::GetLastErrorMessage();
		vk.reset();
		context = nullptr;
		return false;
	}

	available = true;
	last_error = "";
	return true;
}

void NtcRuntime::shutdown() {
	if (context) {
		ntc::DestroyContext(context);
		context = nullptr;
	}
	if (vk) {
		vk->shutdown();
		vk.reset();
	}
	available = false;
	initialized = false;
}

bool NtcRuntime::is_available() const {
	return available;
}

int NtcRuntime::get_capability_tier() const {
	return available ? 1 : 0;
}

String NtcRuntime::get_last_error() const {
	return last_error;
}

Dictionary NtcRuntime::get_library_version() const {
	Dictionary d;
	if (!available) {
		return d;
	}
	ntc::VersionInfo v = ntc::GetLibraryVersion();
	d["major"] = v.major;
	d["minor"] = v.minor;
	d["point"] = v.point;
	d["branch"] = v.branch ? String(v.branch) : String();
	d["commit"] = v.commitHash ? String(v.commitHash) : String();
	return d;
}

String NtcRuntime::get_gpu_name() const {
	if (!vk) {
		return String();
	}
	return String(vk->gpu_props.deviceName);
}

bool NtcRuntime::get_coop_vec_enabled() const {
	return vk && vk->coop_vec_enabled;
}

NtcVkDevice *NtcRuntime::vk_device() {
	return vk.get();
}

ntc::IContext *NtcRuntime::ntc_context() {
	return context;
}
