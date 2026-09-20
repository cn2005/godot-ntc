#include "ntc_material.h"

#include "ntc_runtime.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <vector>

using namespace godot;

namespace {

String native_from_res(const String &res_path) {
	return ProjectSettings::get_singleton()->globalize_path(res_path);
}

bool image_has_alpha(const Ref<Image> &img) {
	if (img.is_null()) {
		return false;
	}
	const PackedByteArray data = img->get_data();
	const int n = img->get_width() * img->get_height() * 4;
	const int limit = data.size() < n ? data.size() : n;
	for (int i = 3; i < limit; i += 4) {
		if (data[i] < 255) {
			return true;
		}
	}
	return false;
}

int full_mip_count(int width, int height) {
	int mips = 1;
	int w = width;
	int h = height;
	while (w > 1 || h > 1) {
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
		++mips;
	}
	return mips;
}

Ref<Image> prepare_rgba8_with_mips(const Ref<Image> &src, int width, int height, bool renormalize, String &error,
		const char *semantic) {
	if (src.is_null() || src->is_empty()) {
		error = String("Cannot read pixels from ") + semantic;
		return {};
	}
	Ref<Image> img = src->duplicate();
	if (img->is_compressed()) {
		if (img->decompress() != OK) {
			error = String("Failed to decompress ") + semantic;
			return {};
		}
	}
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	const bool size_changed = img->get_width() != width || img->get_height() != height;
	if (size_changed) {
		if (img->has_mipmaps()) {
			img->clear_mipmaps();
		}
		img->resize(width, height);
	}
	if (!img->has_mipmaps()) {
		if (img->generate_mipmaps(renormalize) != OK) {
			error = String("Failed to generate mipmaps for ") + semantic;
			return {};
		}
	}
	return img;
}

bool extract_mip_chain(const Ref<Image> &img, int width, int height, std::vector<std::vector<uint8_t>> &out,
		String &error, const char *semantic) {
	const PackedByteArray pixels = img->get_data();
	const int max_mips = full_mip_count(width, height);
	int mips = 1;
	if (img->has_mipmaps()) {
		mips = 1 + img->get_mipmap_count();
		if (mips > max_mips) {
			mips = img->get_mipmap_count();
		}
		mips = std::min(mips, max_mips);
	}
	out.clear();
	out.resize(mips);
	int64_t offset = 0;
	for (int mip = 0; mip < mips; ++mip) {
		const int mw = std::max(1, width >> mip);
		const int mh = std::max(1, height >> mip);
		const int64_t bytes = int64_t(mw) * int64_t(mh) * 4;
		if (offset + bytes > pixels.size()) {
			error = vformat("%s mip %d out of range (offset=%d size=%d data=%d)", semantic, mip, offset, bytes,
					pixels.size());
			return false;
		}
		out[mip].assign(pixels.ptr() + offset, pixels.ptr() + offset + bytes);
		offset += bytes;
	}
	return true;
}

} // namespace

const NTCSMaterial3D::SlotSpec *NTCSMaterial3D::slot_specs(int &count) {
	static const SlotSpec kSpecs[] = {
		{ TEXTURE_ALBEDO, "albedo", 3, true, 7 },
		{ TEXTURE_NORMAL, "normal", 3, false, 7 },
		{ TEXTURE_ROUGHNESS, "roughness", 1, false, 4 },
		{ TEXTURE_METALLIC, "metallic", 1, false, 4 },
		{ TEXTURE_AMBIENT_OCCLUSION, "ao", 1, false, 4 },
		{ TEXTURE_EMISSION, "emission", 3, true, 7 },
	};
	count = int(sizeof(kSpecs) / sizeof(kSpecs[0]));
	return kSpecs;
}

void NTCSMaterial3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_ntc_mode", "mode"), &NTCSMaterial3D::set_ntc_mode);
	ClassDB::bind_method(D_METHOD("get_ntc_mode"), &NTCSMaterial3D::get_ntc_mode);
	ClassDB::bind_method(D_METHOD("set_ntc_texture_set", "set"), &NTCSMaterial3D::set_ntc_texture_set);
	ClassDB::bind_method(D_METHOD("get_ntc_texture_set"), &NTCSMaterial3D::get_ntc_texture_set);
	ClassDB::bind_method(D_METHOD("set_ntc_bits_per_pixel", "bpp"), &NTCSMaterial3D::set_ntc_bits_per_pixel);
	ClassDB::bind_method(D_METHOD("get_ntc_bits_per_pixel"), &NTCSMaterial3D::get_ntc_bits_per_pixel);
	ClassDB::bind_method(D_METHOD("set_ntc_training_steps", "steps"), &NTCSMaterial3D::set_ntc_training_steps);
	ClassDB::bind_method(D_METHOD("get_ntc_training_steps"), &NTCSMaterial3D::get_ntc_training_steps);
	ClassDB::bind_method(D_METHOD("set_ntc_status", "status"), &NTCSMaterial3D::set_ntc_status);
	ClassDB::bind_method(D_METHOD("get_ntc_status"), &NTCSMaterial3D::get_ntc_status);
	ClassDB::bind_method(D_METHOD("set_ntc_source_signature", "signature"), &NTCSMaterial3D::set_ntc_source_signature);
	ClassDB::bind_method(D_METHOD("get_ntc_source_signature"), &NTCSMaterial3D::get_ntc_source_signature);
	ClassDB::bind_method(D_METHOD("set_texture", "param", "texture"), &NTCSMaterial3D::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture", "param"), &NTCSMaterial3D::get_texture);
	ClassDB::bind_method(D_METHOD("notify_sources_changed"), &NTCSMaterial3D::notify_sources_changed);
	ClassDB::bind_method(D_METHOD("rebuild_ntc"), &NTCSMaterial3D::rebuild_ntc);
	ClassDB::bind_method(D_METHOD("apply_decoded"), &NTCSMaterial3D::apply_decoded);
	ClassDB::bind_method(D_METHOD("get_pack_excluded_paths"), &NTCSMaterial3D::get_pack_excluded_paths);
	ClassDB::bind_method(D_METHOD("create_export_copy"), &NTCSMaterial3D::create_export_copy);
	ClassDB::bind_method(D_METHOD("finish_compress"), &NTCSMaterial3D::finish_compress);
	ClassDB::bind_method(D_METHOD("report_compress_log", "step", "total", "loss", "message"),
			&NTCSMaterial3D::report_compress_log);
	ClassDB::bind_method(D_METHOD("poll_compress_tick"), &NTCSMaterial3D::poll_compress_tick);
	ClassDB::bind_method(D_METHOD("on_debounce_timeout", "gen"), &NTCSMaterial3D::on_debounce_timeout);

	ADD_GROUP("NTC", "ntc_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ntc_mode", PROPERTY_HINT_ENUM, "None,On Load"), "set_ntc_mode",
			"get_ntc_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "ntc_texture_set", PROPERTY_HINT_RESOURCE_TYPE, "NTCTextureSet",
						 PROPERTY_USAGE_NO_EDITOR),
			"set_ntc_texture_set", "get_ntc_texture_set");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ntc_bits_per_pixel", PROPERTY_HINT_RANGE, "1,20,0.1"),
			"set_ntc_bits_per_pixel", "get_ntc_bits_per_pixel");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ntc_training_steps", PROPERTY_HINT_RANGE, "1000,100000,1000"),
			"set_ntc_training_steps", "get_ntc_training_steps");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "ntc_status", PROPERTY_HINT_NONE, "",
						 PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
			"set_ntc_status", "get_ntc_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "ntc_source_signature", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR),
			"set_ntc_source_signature", "get_ntc_source_signature");

	ADD_SIGNAL(MethodInfo("ntc_file_written", PropertyInfo(Variant::STRING, "path")));

	BIND_ENUM_CONSTANT(NTC_MODE_NONE);
	BIND_ENUM_CONSTANT(NTC_MODE_ON_LOAD);
}

NTCSMaterial3D::NTCSMaterial3D() = default;

NTCSMaterial3D::~NTCSMaterial3D() {
	cancel_compress.store(true);
	if (compress_job) {
		compress_job->cancel.store(true);
	}
	join_worker();
}

void NTCSMaterial3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == StringName("ntc_bits_per_pixel") || p_property.name == StringName("ntc_training_steps")) {
		if (ntc_mode != NTC_MODE_ON_LOAD) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}
}

void NTCSMaterial3D::set_status(const String &status) {
	set_ntc_status(status);
}

void NTCSMaterial3D::report_compress_log(int step, int total, float loss, const String &message) {
	String line = message;
	if (total > 0 && step > 0) {
		line += vformat("  (%d/%d, %.0f%%, loss=%.5f)", step, total, 100.0 * double(step) / double(total), loss);
	}
	set_ntc_status(line);
}

void NTCSMaterial3D::set_ntc_status(const String &status) {
	ntc_status = status;
	if (!status.is_empty()) {
		UtilityFunctions::print(String("NTC: ") + status);
	}
	call_deferred("notify_property_list_changed");
}

void NTCSMaterial3D::set_ntc_source_signature(const String &signature) {
	ntc_source_signature = signature;
}

void NTCSMaterial3D::set_ntc_texture_set(const Ref<NTCTextureSet> &set) {
	ntc_texture_set = set;
	if (ntc_mode == NTC_MODE_ON_LOAD && !Engine::get_singleton()->is_editor_hint()) {
		apply_decoded();
	}
}

void NTCSMaterial3D::set_ntc_bits_per_pixel(float bpp) {
	ntc_bits_per_pixel = std::clamp(bpp, 1.f, 20.f);
	if (ntc_mode == NTC_MODE_ON_LOAD) {
		notify_sources_changed();
	}
}

void NTCSMaterial3D::set_ntc_training_steps(int steps) {
	ntc_training_steps = std::clamp(steps, 1000, 100000);
	if (ntc_mode == NTC_MODE_ON_LOAD) {
		notify_sources_changed();
	}
}

void NTCSMaterial3D::set_texture(BaseMaterial3D::TextureParam p_param, const Ref<Texture2D> &p_texture) {
	StandardMaterial3D::set_texture(p_param, p_texture);
	if (!applying_decoded && !skip_rebuild && Engine::get_singleton()->is_editor_hint() &&
			ntc_mode == NTC_MODE_ON_LOAD) {
		notify_sources_changed();
	}
}

Ref<Texture2D> NTCSMaterial3D::get_texture(BaseMaterial3D::TextureParam p_param) const {
	return StandardMaterial3D::get_texture(p_param);
}

void NTCSMaterial3D::set_ntc_mode(NtcMode mode) {
	if (ntc_mode == mode) {
		return;
	}
	ntc_mode = mode;
	call_deferred("notify_property_list_changed");

	if (mode == NTC_MODE_NONE) {
		cancel_compress.store(true);
		if (compress_job) {
			compress_job->cancel.store(true);
		}
		set_status("");
		return;
	}

	if (!Engine::get_singleton()->is_editor_hint()) {
		if (ntc_texture_set.is_valid()) {
			if (apply_decoded()) {
				set_status("Decoded on load");
			} else {
				set_status(ntc_texture_set->get_last_error());
			}
		}
		return;
	}

	if (should_reuse_existing_ntc()) {
		adopt_source_signature_if_empty();
		set_status("Using existing .ntc");
		return;
	}
	if (collect_source_count() == 0) {
		set_status("ON_LOAD needs source textures or an existing .ntc");
		return;
	}
	call_deferred("rebuild_ntc");
}

bool NTCSMaterial3D::should_reuse_existing_ntc() const {
	if (ntc_texture_set.is_null() || !ntc_texture_set->has_payload()) {
		return false;
	}
	if (collect_source_count() == 0 || ntc_source_signature.is_empty()) {
		return true;
	}
	return compute_source_signature() == ntc_source_signature;
}

void NTCSMaterial3D::adopt_source_signature_if_empty() {
	if (ntc_source_signature.is_empty() && collect_source_count() > 0) {
		ntc_source_signature = compute_source_signature();
	}
}

int NTCSMaterial3D::collect_source_count() const {
	int specs_n = 0;
	const SlotSpec *specs = slot_specs(specs_n);
	int count = 0;
	for (int i = 0; i < specs_n; ++i) {
		if (StandardMaterial3D::get_texture(specs[i].slot).is_valid()) {
			++count;
		}
	}
	return count;
}

String NTCSMaterial3D::compute_source_signature() const {
	String s = String::num(ntc_bits_per_pixel, 2) + "|" + String::num_int64(ntc_training_steps);
	int specs_n = 0;
	const SlotSpec *specs = slot_specs(specs_n);
	for (int i = 0; i < specs_n; ++i) {
		s += "|";
		Ref<Texture2D> tex = StandardMaterial3D::get_texture(specs[i].slot);
		if (tex.is_valid()) {
			s += tex->get_path() + ":" + String::num_int64(tex->get_width()) + "x" +
					String::num_int64(tex->get_height());
		}
	}
	return s;
}

String NTCSMaterial3D::resolve_ntc_res_path() const {
	const String path = get_path();
	if (path.begins_with("res://") && !path.contains("::") && path.get_file().get_basename() != "") {
		return path.get_basename() + ".ntc";
	}
	return String("res://.ntc_cache/") + String::num_int64(get_instance_id()) + ".ntc";
}

bool NTCSMaterial3D::collect_compress_request(NtcCompressRequest &out, Dictionary &semantic, String &error) const {
	int specs_n = 0;
	const SlotSpec *specs = slot_specs(specs_n);

	Ref<Image> first;
	String first_name;
	for (int i = 0; i < specs_n; ++i) {
		Ref<Texture2D> tex = StandardMaterial3D::get_texture(specs[i].slot);
		if (tex.is_null()) {
			continue;
		}
		Ref<Image> img = tex->get_image();
		if (img.is_null() || img->is_empty()) {
			error = String("Cannot read pixels from ") + specs[i].semantic + " (need a disk texture, not a viewport).";
			return false;
		}
		first = img;
		first_name = specs[i].semantic;
		break;
	}
	if (first.is_null()) {
		error = "No source textures on this material.";
		return false;
	}

	int width = first->get_width();
	int height = first->get_height();
	width = (width + 3) & ~3;
	height = (height + 3) & ~3;
	if (width < 4 || height < 4) {
		error = "Textures must be at least 4x4 after alignment.";
		return false;
	}

	out.width = width;
	out.height = height;
	out.mips = std::min(full_mip_count(width, height), 16);
	out.bits_per_pixel = ntc_bits_per_pixel;
	out.training_steps = ntc_training_steps;
	out.textures.clear();
	semantic.clear();

	for (int i = 0; i < specs_n; ++i) {
		Ref<Texture2D> tex = StandardMaterial3D::get_texture(specs[i].slot);
		if (tex.is_null()) {
			continue;
		}
		const bool renormalize = specs[i].slot == TEXTURE_NORMAL;
		Ref<Image> img = prepare_rgba8_with_mips(tex->get_image(), width, height, renormalize, error, specs[i].semantic);
		if (img.is_null()) {
			return false;
		}

		NtcCompressChannel ch;
		ch.semantic = specs[i].semantic;
		ch.name = specs[i].semantic;
		ch.srgb = specs[i].srgb;
		ch.bc_format = specs[i].bc_format;
		ch.num_channels = specs[i].default_channels;
		if (specs[i].slot == TEXTURE_ALBEDO && image_has_alpha(img)) {
			ch.num_channels = 4;
		}
		if (!extract_mip_chain(img, width, height, ch.mip_rgba8, error, specs[i].semantic)) {
			return false;
		}
		if (int(ch.mip_rgba8.size()) > out.mips) {
			ch.mip_rgba8.resize(out.mips);
		}
		out.textures.push_back(std::move(ch));
		semantic[specs[i].semantic] = String(specs[i].semantic);
	}

	if (out.textures.empty()) {
		error = "No readable source textures.";
		return false;
	}
	return true;
}

void NTCSMaterial3D::notify_sources_changed() {
	if (skip_rebuild || ntc_mode != NTC_MODE_ON_LOAD || !Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	++debounce_gen;
	const uint64_t gen = debounce_gen;
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!tree) {
		rebuild_ntc();
		return;
	}
	Ref<SceneTreeTimer> timer = tree->create_timer(0.8);
	if (timer.is_valid()) {
		timer->connect("timeout", callable_mp(this, &NTCSMaterial3D::on_debounce_timeout).bind(gen));
	} else {
		rebuild_ntc();
	}
}

void NTCSMaterial3D::on_debounce_timeout(uint64_t gen) {
	if (gen != debounce_gen) {
		return;
	}
	rebuild_ntc();
}

void NTCSMaterial3D::rebuild_ntc() {
	if (skip_rebuild || ntc_mode != NTC_MODE_ON_LOAD) {
		return;
	}
	if (should_reuse_existing_ntc()) {
		adopt_source_signature_if_empty();
		set_status("Using existing .ntc");
		return;
	}
	if (collect_source_count() == 0) {
		set_status("ON_LOAD needs source textures or an existing .ntc");
		return;
	}
	start_compress();
}

void NTCSMaterial3D::start_compress() {
	if (compressing.load()) {
		cancel_compress.store(true);
		if (compress_job) {
			compress_job->cancel.store(true);
		}
		rebuild_queued = true;
		set_status("Waiting to overwrite .ntc…");
		return;
	}

	NtcCompressRequest request;
	Dictionary semantic;
	String error;
	if (!collect_compress_request(request, semantic, error)) {
		set_status(error);
		return;
	}
	UtilityFunctions::print(vformat("NTC: collected %d textures at %dx%d, mips=%d, bpp=%.1f, steps=%d",
			int(request.textures.size()), request.width, request.height, request.mips, ntc_bits_per_pixel,
			ntc_training_steps));

	const String res_path = resolve_ntc_res_path();
	if (res_path.begins_with("res://.ntc_cache/")) {
		DirAccess::make_dir_recursive_absolute(native_from_res("res://.ntc_cache"));
	}
	request.output_path = native_from_res(res_path).utf8().get_data();

	CompressOutcome pending;
	pending.output_res_path = res_path.utf8().get_data();
	pending.output_native_path = request.output_path;
	pending.semantic_map = semantic;
	pending.bits_per_pixel = ntc_bits_per_pixel;
	pending.signature = compute_source_signature();
	{
		std::lock_guard<std::mutex> lock(outcome_mu);
		outcome = pending;
	}

	cancel_compress.store(false);
	compressing.store(true);
	if (NtcRuntime *rt = NtcRuntime::get_singleton()) {
		if (!rt->ensure_library_loaded()) {
			set_status(rt->get_last_error());
			compressing.store(false);
			return;
		}
	}
	set_status("Compressing " + res_path + " (CUDA, " + String::num_int64(ntc_training_steps) + " steps)…");

	auto job = std::make_shared<CompressShared>();
	job->request = std::move(request);
	job->request.on_log = [job](const NtcCompressProgress &p, const char *msg) {
		job->step.store(p.step);
		job->total.store(p.total);
		{
			std::lock_guard<std::mutex> lock(job->mu);
			job->log = msg ? msg : "";
			job->loss = p.loss;
		}
		job->log_gen.fetch_add(1);
	};
	compress_job = job;
	last_log_gen = 0;

	join_worker();
	worker = std::make_unique<std::thread>([job]() {
		std::string err;
		NtcCompressProgress progress;
		const bool ok = ntc_compress_texture_set(job->request, &job->cancel, &progress, err);
		{
			std::lock_guard<std::mutex> lock(job->mu);
			job->ok = ok;
			job->error = err;
		}
		job->finished.store(true);
	});
	schedule_compress_poll();
}

void NTCSMaterial3D::schedule_compress_poll() {
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (!tree) {
		return;
	}
	Ref<SceneTreeTimer> timer = tree->create_timer(0.25);
	if (timer.is_valid()) {
		timer->connect("timeout", callable_mp(this, &NTCSMaterial3D::poll_compress_tick));
	}
}

void NTCSMaterial3D::poll_compress_tick() {
	if (!compress_job) {
		return;
	}
	const int gen = compress_job->log_gen.load();
	if (gen != last_log_gen) {
		last_log_gen = gen;
		std::string log;
		float loss = 0.f;
		{
			std::lock_guard<std::mutex> lock(compress_job->mu);
			log = compress_job->log;
			loss = compress_job->loss;
		}
		report_compress_log(compress_job->step.load(), compress_job->total.load(), loss, String(log.c_str()));
	}
	if (compress_job->finished.load()) {
		finish_compress();
		return;
	}
	if (compressing.load()) {
		schedule_compress_poll();
	}
}

void NTCSMaterial3D::finish_compress() {
	join_worker();
	compressing.store(false);

	CompressOutcome done;
	{
		std::lock_guard<std::mutex> lock(outcome_mu);
		done = outcome;
	}
	if (compress_job) {
		std::lock_guard<std::mutex> lock(compress_job->mu);
		done.ok = compress_job->ok;
		done.error = compress_job->error;
	}

	if (rebuild_queued) {
		rebuild_queued = false;
		if (ntc_mode == NTC_MODE_ON_LOAD && !skip_rebuild) {
			start_compress();
			return;
		}
	}

	if (!done.ok) {
		set_status(String("Compress failed: ") + done.error.c_str());
		return;
	}

	if (ntc_texture_set.is_null()) {
		ntc_texture_set.instantiate();
	}
	ntc_texture_set->set_embedded_bytes(PackedByteArray());
	ntc_texture_set->set_file_path(String(done.output_res_path.c_str()));
	ntc_texture_set->set_semantic_map(done.semantic_map);
	ntc_texture_set->set_bits_per_pixel(done.bits_per_pixel);
	ntc_source_signature = done.signature;
	set_status("Wrote " + String(done.output_res_path.c_str()) + " (sources kept)");
	emit_changed();
	emit_signal("ntc_file_written", String(done.output_res_path.c_str()));
}

void NTCSMaterial3D::join_worker() {
	if (worker && worker->joinable()) {
		if (std::this_thread::get_id() == worker->get_id()) {
			worker->detach();
			worker.reset();
			return;
		}
		worker->join();
		worker.reset();
	}
}

bool NTCSMaterial3D::apply_decoded() {
	if (ntc_mode != NTC_MODE_ON_LOAD) {
		return false;
	}
	if (ntc_texture_set.is_null()) {
		return false;
	}
	if (ntc_texture_set->ensure_decoded() != OK) {
		return false;
	}
	applying_decoded = true;
	const Error err = ntc_texture_set->apply_to_standard_material(this);
	applying_decoded = false;
	return err == OK;
}

PackedStringArray NTCSMaterial3D::get_pack_excluded_paths() const {
	PackedStringArray paths;
	if (ntc_mode != NTC_MODE_ON_LOAD) {
		return paths;
	}
	int specs_n = 0;
	const SlotSpec *specs = slot_specs(specs_n);
	for (int i = 0; i < specs_n; ++i) {
		Ref<Texture2D> tex = StandardMaterial3D::get_texture(specs[i].slot);
		if (tex.is_null()) {
			continue;
		}
		const String path = tex->get_path();
		if (path.begins_with("res://") && !path.contains("::")) {
			paths.append(path);
		}
	}
	return paths;
}

Ref<NTCSMaterial3D> NTCSMaterial3D::create_export_copy() {
	Ref<NTCSMaterial3D> copy = duplicate(true);
	if (copy.is_null()) {
		return {};
	}
	copy->skip_rebuild = true;
	copy->cancel_compress.store(true);
	if (ntc_texture_set.is_valid()) {
		Ref<NTCTextureSet> slim;
		slim.instantiate();
		slim->set_file_path(ntc_texture_set->get_file_path());
		slim->set_semantic_map(ntc_texture_set->get_semantic_map());
		slim->set_bits_per_pixel(ntc_texture_set->get_bits_per_pixel());
		copy->ntc_texture_set = slim;
	}
	int specs_n = 0;
	const SlotSpec *specs = slot_specs(specs_n);
	copy->applying_decoded = true;
	for (int i = 0; i < specs_n; ++i) {
		copy->StandardMaterial3D::set_texture(specs[i].slot, Ref<Texture2D>());
	}
	copy->applying_decoded = false;
	copy->set_status("Export copy: source textures stripped");
	return copy;
}
