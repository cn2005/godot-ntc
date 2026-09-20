#pragma once

#include "ntc_compress.h"
#include "ntc_texture_set.h"

#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class NTCSMaterial3D : public godot::StandardMaterial3D {
	GDCLASS(NTCSMaterial3D, godot::StandardMaterial3D);

public:
	enum NtcMode {
		NTC_MODE_NONE = 0,
		NTC_MODE_ON_LOAD = 1,
	};

protected:
	static void _bind_methods();
	void _validate_property(godot::PropertyInfo &p_property) const;

public:
	NTCSMaterial3D();
	~NTCSMaterial3D() override;

	void set_ntc_mode(NtcMode mode);
	NtcMode get_ntc_mode() const { return ntc_mode; }

	void set_ntc_texture_set(const godot::Ref<NTCTextureSet> &set);
	godot::Ref<NTCTextureSet> get_ntc_texture_set() const { return ntc_texture_set; }

	void set_ntc_bits_per_pixel(float bpp);
	float get_ntc_bits_per_pixel() const { return ntc_bits_per_pixel; }

	void set_ntc_training_steps(int steps);
	int get_ntc_training_steps() const { return ntc_training_steps; }

	void set_ntc_status(const godot::String &status);
	godot::String get_ntc_status() const { return ntc_status; }
	void set_ntc_source_signature(const godot::String &signature);
	godot::String get_ntc_source_signature() const { return ntc_source_signature; }

	void set_texture(godot::BaseMaterial3D::TextureParam p_param, const godot::Ref<godot::Texture2D> &p_texture);
	godot::Ref<godot::Texture2D> get_texture(godot::BaseMaterial3D::TextureParam p_param) const;

	void notify_sources_changed();
	void rebuild_ntc();
	bool apply_decoded();

	godot::PackedStringArray get_pack_excluded_paths() const;
	godot::Ref<NTCSMaterial3D> create_export_copy();

private:
	NtcMode ntc_mode = NTC_MODE_NONE;
	godot::Ref<NTCTextureSet> ntc_texture_set;
	float ntc_bits_per_pixel = 5.f;
	int ntc_training_steps = 20000;
	godot::String ntc_status;
	godot::String ntc_source_signature;

	bool applying_decoded = false;
	bool skip_rebuild = false;
	bool rebuild_queued = false;
	uint64_t debounce_gen = 0;

	std::atomic<bool> cancel_compress{ false };
	std::atomic<bool> compressing{ false };
	std::unique_ptr<std::thread> worker;
	int last_log_gen = 0;

	struct CompressShared {
		NtcCompressRequest request;
		std::atomic<bool> cancel{ false };
		std::atomic<bool> finished{ false };
		std::atomic<int> step{ 0 };
		std::atomic<int> total{ 0 };
		std::atomic<int> log_gen{ 0 };
		std::mutex mu;
		std::string log;
		float loss = 0.f;
		bool ok = false;
		std::string error;
	};
	std::shared_ptr<CompressShared> compress_job;

	struct CompressOutcome {
		bool ok = false;
		std::string error;
		std::string output_res_path;
		std::string output_native_path;
		godot::Dictionary semantic_map;
		float bits_per_pixel = 5.f;
		godot::String signature;
	};
	std::mutex outcome_mu;
	CompressOutcome outcome;

	struct SlotSpec {
		godot::BaseMaterial3D::TextureParam slot;
		const char *semantic;
		int default_channels;
		bool srgb;
		int bc_format;
	};

	static const SlotSpec *slot_specs(int &count);
	godot::String compute_source_signature() const;
	int collect_source_count() const;
	bool should_reuse_existing_ntc() const;
	void adopt_source_signature_if_empty();
	godot::String resolve_ntc_res_path() const;
	bool collect_compress_request(NtcCompressRequest &out, godot::Dictionary &semantic, godot::String &error) const;
	void set_status(const godot::String &status);
	void start_compress();
	void finish_compress();
	void report_compress_log(int step, int total, float loss, const godot::String &message);
	void poll_compress_tick();
	void schedule_compress_poll();
	void on_debounce_timeout(uint64_t gen);
	void join_worker();
};

VARIANT_ENUM_CAST(NTCSMaterial3D::NtcMode);
