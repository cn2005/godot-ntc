#include "register_types.h"

#include "ntc_material.h"
#include "ntc_runtime.h"
#include "ntc_texture_set.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/engine.hpp>

using namespace godot;

void initialize_ntc_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(NtcRuntime);
	GDREGISTER_CLASS(NTCTextureSet);
	GDREGISTER_CLASS(NTCSMaterial3D);

	NtcRuntime *runtime = memnew(NtcRuntime);
	NtcRuntime::set_singleton(runtime);
	Engine::get_singleton()->register_singleton("NtcRuntime", runtime);
}

void uninitialize_ntc_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (NtcRuntime *runtime = NtcRuntime::get_singleton()) {
		Engine::get_singleton()->unregister_singleton("NtcRuntime");
		NtcRuntime::set_singleton(nullptr);
		memdelete(runtime);
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT ntc_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_ntc_module);
	init_obj.register_terminator(uninitialize_ntc_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
