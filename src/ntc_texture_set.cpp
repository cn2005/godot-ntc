#include "ntc_texture_set.h"
#include "ntc_runtime.h"
#include "ntc_vk.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void NTCTextureSet::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_file_path", "path"), &NTCTextureSet::set_file_path);
	ClassDB::bind_method(D_METHOD("get_file_path"), &NTCTextureSet::get_file_path);
	ClassDB::bind_method(D_METHOD("set_embedded_bytes", "bytes"), &NTCTextureSet::set_embedded_bytes);
	ClassDB::bind_method(D_METHOD("get_embedded_bytes"), &NTCTextureSet::get_embedded_bytes);
	ClassDB::bind_method(D_METHOD("set_semantic_map", "map"), &NTCTextureSet::set_semantic_map);
	ClassDB::bind_method(D_METHOD("get_semantic_map"), &NTCTextureSet::get_semantic_map);
	ClassDB::bind_method(D_METHOD("set_bits_per_pixel", "bpp"), &NTCTextureSet::set_bits_per_pixel);
	ClassDB::bind_method(D_METHOD("get_bits_per_pixel"), &NTCTextureSet::get_bits_per_pixel);
	ClassDB::bind_method(D_METHOD("load_from_file", "path"), &NTCTextureSet::load_from_file);
	ClassDB::bind_method(D_METHOD("load_from_bytes", "bytes"), &NTCTextureSet::load_from_bytes);
	ClassDB::bind_method(D_METHOD("ensure_decoded"), &NTCTextureSet::ensure_decoded);
	ClassDB::bind_method(D_METHOD("embed_from_file"), &NTCTextureSet::embed_from_file);
	ClassDB::bind_method(D_METHOD("has_payload"), &NTCTextureSet::has_payload);
	ClassDB::bind_method(D_METHOD("get_width"), &NTCTextureSet::get_width);
	ClassDB::bind_method(D_METHOD("get_height"), &NTCTextureSet::get_height);
	ClassDB::bind_method(D_METHOD("get_mip_count"), &NTCTextureSet::get_mip_count);
	ClassDB::bind_method(D_METHOD("get_channel_count"), &NTCTextureSet::get_channel_count);
	ClassDB::bind_method(D_METHOD("get_weight_type"), &NTCTextureSet::get_weight_type);
	ClassDB::bind_method(D_METHOD("get_last_error"), &NTCTextureSet::get_last_error);
	ClassDB::bind_method(D_METHOD("get_texture_names"), &NTCTextureSet::get_texture_names);
	ClassDB::bind_method(D_METHOD("get_texture", "name"), &NTCTextureSet::get_texture);
	ClassDB::bind_method(D_METHOD("get_all_textures"), &NTCTextureSet::get_all_textures);
	ClassDB::bind_method(D_METHOD("apply_to_standard_material", "material"),
			&NTCTextureSet::apply_to_standard_material);
	ClassDB::bind_method(D_METHOD("create_export_copy"), &NTCTextureSet::create_export_copy);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "file_path", PROPERTY_HINT_FILE, "*.ntc"), "set_file_path",
			"get_file_path");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "embedded_bytes", PROPERTY_HINT_NONE, "",
						 PROPERTY_USAGE_NO_EDITOR),
			"set_embedded_bytes", "get_embedded_bytes");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "semantic_map"), "set_semantic_map", "get_semantic_map");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bits_per_pixel"), "set_bits_per_pixel", "get_bits_per_pixel");
}

void NTCTextureSet::clear_decoded() {
	decoded = false;
	textures.clear();
	result = {};
}

void NTCTextureSet::set_file_path(const String &path) {
	if (file_path == path) {
		return;
	}
	file_path = path;
	clear_decoded();
}

void NTCTextureSet::set_embedded_bytes(const PackedByteArray &bytes) {
	embedded_bytes = bytes;
	clear_decoded();
}

void NTCTextureSet::set_semantic_map(const Dictionary &map) {
	semantic_map = map;
}

void NTCTextureSet::set_bits_per_pixel(float bpp) {
	bits_per_pixel = bpp;
}

bool NTCTextureSet::has_payload() const {
	if (embedded_bytes.size() > 0) {
		return true;
	}
	if (file_path.is_empty()) {
		return false;
	}
	const String global = ProjectSettings::get_singleton()->globalize_path(file_path);
	return FileAccess::file_exists(file_path) || FileAccess::file_exists(global);
}

Error NTCTextureSet::embed_from_file() {
	if (embedded_bytes.size() > 0) {
		return OK;
	}
	if (file_path.is_empty()) {
		last_error = "NTCTextureSet has no file_path to embed.";
		return ERR_FILE_NOT_FOUND;
	}
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
	if (f.is_null()) {
		f = FileAccess::open(ProjectSettings::get_singleton()->globalize_path(file_path), FileAccess::READ);
	}
	if (f.is_null()) {
		last_error = "Cannot open " + file_path;
		return ERR_FILE_CANT_OPEN;
	}
	embedded_bytes = f->get_buffer(f->get_length());
	return OK;
}

Error NTCTextureSet::load_from_file(const String &path) {
	set_file_path(path);
	return ensure_decoded();
}

Error NTCTextureSet::load_from_bytes(const PackedByteArray &bytes) {
	set_embedded_bytes(bytes);
	return ensure_decoded();
}

Error NTCTextureSet::ensure_decoded() {
	if (decoded) {
		return OK;
	}
	if (embedded_bytes.size() > 0) {
		return decode_bytes(embedded_bytes.ptr(), size_t(embedded_bytes.size()));
	}
	if (file_path.is_empty()) {
		last_error = "NTCTextureSet has no .ntc payload.";
		return ERR_UNCONFIGURED;
	}
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
	if (f.is_null()) {
		f = FileAccess::open(ProjectSettings::get_singleton()->globalize_path(file_path), FileAccess::READ);
	}
	if (f.is_null()) {
		last_error = "Cannot open " + file_path;
		return ERR_FILE_CANT_OPEN;
	}
	PackedByteArray bytes = f->get_buffer(f->get_length());
	return decode_bytes(bytes.ptr(), size_t(bytes.size()));
}

Error NTCTextureSet::decode_bytes(const uint8_t *data, size_t size) {
	NtcRuntime *rt = NtcRuntime::get_singleton();
	if (!rt) {
		last_error = "NtcRuntime singleton missing.";
		return ERR_UNAVAILABLE;
	}
	if (!rt->initialize()) {
		last_error = rt->get_last_error();
		return ERR_UNAVAILABLE;
	}

	std::string err;
	NtcDecodeResult decoded_result;
	if (!ntc_decode_file_a1(*rt->vk_device(), rt->ntc_context(), data, size, decoded_result, err)) {
		last_error = String(err.c_str());
		return FAILED;
	}
	result = std::move(decoded_result);
	if (result.bits_per_pixel > 0.f) {
		bits_per_pixel = result.bits_per_pixel;
	}
	rebuild_textures();
	decoded = true;
	last_error = "";
	return OK;
}

void NTCTextureSet::rebuild_textures() {
	textures.clear();
	for (const NtcDecodedTexture &src : result.textures) {
		Ref<Image> img = Image::create_from_data(src.width, src.height, src.mips > 1, src.image_format, src.data);
		if (img.is_null() || img->is_empty()) {
			UtilityFunctions::push_warning("NTC: failed to create Image for " + src.name);
			continue;
		}
		Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
		textures[src.name] = tex;
	}
}

PackedStringArray NTCTextureSet::get_texture_names() const {
	PackedStringArray names;
	Array keys = textures.keys();
	for (int i = 0; i < keys.size(); ++i) {
		names.append(keys[i]);
	}
	return names;
}

Ref<Texture2D> NTCTextureSet::get_texture(const String &name) const {
	if (!textures.has(name)) {
		return {};
	}
	return textures[name];
}

Dictionary NTCTextureSet::get_all_textures() const {
	return textures;
}

Ref<Texture2D> NTCTextureSet::find_by_semantic(const char *semantic, const char *const *keys) const {
	if (semantic_map.has(semantic)) {
		const String mapped = semantic_map[semantic];
		if (textures.has(mapped)) {
			return textures[mapped];
		}
	}
	Array names = textures.keys();
	for (int i = 0; i < names.size(); ++i) {
		const String n = String(names[i]).to_lower();
		for (const char *const *k = keys; *k; ++k) {
			if (n.contains(String(*k))) {
				return textures[names[i]];
			}
		}
	}
	return {};
}

Error NTCTextureSet::apply_to_standard_material(const Ref<StandardMaterial3D> &material) const {
	if (material.is_null()) {
		return ERR_INVALID_PARAMETER;
	}
	static const char *albedo_keys[] = { "ntcsem:albedo", "albedo", "diffuse", "basecolor", "base_color", "color",
		nullptr };
	static const char *normal_keys[] = { "ntcsem:normal", "normal", nullptr };
	static const char *rough_keys[] = { "ntcsem:rough", "rough", nullptr };
	static const char *metal_keys[] = { "ntcsem:metal", "metal", nullptr };
	static const char *ao_keys[] = { "ntcsem:occlusion", "occlusion", "ambient", "ao", nullptr };
	static const char *emissive_keys[] = { "emiss", nullptr };

	if (Ref<Texture2D> t = find_by_semantic("albedo", albedo_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
	}
	if (Ref<Texture2D> t = find_by_semantic("normal", normal_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, t);
		material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
	}
	if (Ref<Texture2D> t = find_by_semantic("roughness", rough_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, t);
	}
	if (Ref<Texture2D> t = find_by_semantic("metallic", metal_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_METALLIC, t);
	}
	if (Ref<Texture2D> t = find_by_semantic("ao", ao_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, t);
		material->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, true);
	}
	if (Ref<Texture2D> t = find_by_semantic("emission", emissive_keys); t.is_valid()) {
		material->set_texture(BaseMaterial3D::TEXTURE_EMISSION, t);
		material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
	}
	return OK;
}

Ref<NTCTextureSet> NTCTextureSet::create_export_copy() {
	Ref<NTCTextureSet> copy;
	copy.instantiate();
	copy->file_path = file_path;
	copy->semantic_map = semantic_map;
	copy->bits_per_pixel = bits_per_pixel;
	return copy;
}
