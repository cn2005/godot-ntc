#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <memory>
#include <string>

class NtcVkDevice;
namespace ntc {
class IContext;
}

class NtcRuntime : public godot::Object {
	GDCLASS(NtcRuntime, godot::Object);

protected:
	static void _bind_methods();

public:
	NtcRuntime();
	~NtcRuntime() override;

	static NtcRuntime *get_singleton();
	static void set_singleton(NtcRuntime *runtime);

	bool initialize();
	bool ensure_library_loaded();
	void shutdown();
	bool is_available() const;
	int get_capability_tier() const;
	godot::String get_last_error() const;
	godot::Dictionary get_library_version() const;
	godot::String get_gpu_name() const;
	bool get_coop_vec_enabled() const;

	NtcVkDevice *vk_device();
	ntc::IContext *ntc_context();

private:
	static NtcRuntime *singleton;

	bool initialized = false;
	bool available = false;
	godot::String last_error;
	std::unique_ptr<NtcVkDevice> vk;
	ntc::IContext *context = nullptr;

	bool load_libntc_dll(std::string &out_error);
};
