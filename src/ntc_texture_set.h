#pragma once

#include "ntc_decode.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

class NTCTextureSet : public godot::Resource {
	GDCLASS(NTCTextureSet, godot::Resource);

protected:
	static void _bind_methods();

public:
	void set_file_path(const godot::String &path);
	godot::String get_file_path() const { return file_path; }

	void set_embedded_bytes(const godot::PackedByteArray &bytes);
	godot::PackedByteArray get_embedded_bytes() const { return embedded_bytes; }

	void set_semantic_map(const godot::Dictionary &map);
	godot::Dictionary get_semantic_map() const { return semantic_map; }

	void set_bits_per_pixel(float bpp);
	float get_bits_per_pixel() const { return bits_per_pixel; }

	godot::Error load_from_file(const godot::String &path);
	godot::Error load_from_bytes(const godot::PackedByteArray &bytes);
	godot::Error ensure_decoded();
	godot::Error embed_from_file();
	bool has_payload() const;

	int get_width() const { return result.width; }
	int get_height() const { return result.height; }
	int get_mip_count() const { return result.mips; }
	int get_channel_count() const { return result.channels; }
	godot::String get_weight_type() const { return result.weight_type; }
	godot::String get_last_error() const { return last_error; }

	godot::PackedStringArray get_texture_names() const;
	godot::Ref<godot::Texture2D> get_texture(const godot::String &name) const;
	godot::Dictionary get_all_textures() const;
	godot::Error apply_to_standard_material(const godot::Ref<godot::StandardMaterial3D> &material) const;

	godot::Ref<NTCTextureSet> create_export_copy();

private:
	godot::String file_path;
	godot::PackedByteArray embedded_bytes;
	godot::Dictionary semantic_map;
	float bits_per_pixel = 0.f;

	NtcDecodeResult result;
	godot::String last_error;
	godot::Dictionary textures;
	bool decoded = false;

	godot::Error decode_bytes(const uint8_t *data, size_t size);
	void rebuild_textures();
	void clear_decoded();
	godot::Ref<godot::Texture2D> find_by_semantic(const char *semantic, const char *const *keys) const;
};
