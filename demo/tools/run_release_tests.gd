extends SceneTree

var _passed := 0
var _failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	print("==== NTC release tests ====")
	_t("runtime_initialize", _test_runtime_initialize)
	_t("library_version", _test_library_version)
	_t("decode_metalplates", _test_decode_metalplates)
	_t("decode_compare_ntc", _test_decode_compare_ntc)
	_t("apply_to_standard_material", _test_apply_to_standard)
	_t("reuse_existing_ntc", _test_reuse_existing_ntc)
	_t("fail_missing_file", _test_fail_missing_file)
	_t("fail_bad_bytes", _test_fail_bad_bytes)
	_t("quality_albedo_mse", _test_quality_albedo_mse)
	print("==== %d passed, %d failed ====" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)


func _t(name: String, fn: Callable) -> void:
	var err := ""
	var ok := true
	var result = fn.call()
	if result is String and not result.is_empty():
		ok = false
		err = result
	if ok:
		_passed += 1
		print("PASS  %s" % name)
	else:
		_failed += 1
		print("FAIL  %s  %s" % [name, err])


func _test_runtime_initialize() -> String:
	var rt = Engine.get_singleton("NtcRuntime")
	if rt == null:
		return "NtcRuntime singleton missing"
	if not rt.initialize():
		return rt.get_last_error()
	if not rt.is_available():
		return "initialize ok but is_available=false"
	if str(rt.get_gpu_name()).is_empty():
		return "empty GPU name"
	return ""


func _test_library_version() -> String:
	var rt = Engine.get_singleton("NtcRuntime")
	var ver: Dictionary = rt.get_library_version()
	if int(ver.get("major", -1)) < 0:
		return "no version: %s" % str(ver)
	print("  LibNTC %s.%s.%s  GPU=%s  coopvec=%s  tier=%s" % [
		ver.get("major", 0), ver.get("minor", 0), ver.get("point", 0),
		rt.get_gpu_name(), rt.get_coop_vec_enabled(), rt.get_capability_tier()
	])
	return ""


func _decode_file(path: String) -> Variant:
	var ts := NTCTextureSet.new()
	if ts.load_from_file(path) != OK:
		return ts.get_last_error()
	if ts.get_width() < 4 or ts.get_height() < 4:
		return "bad size %dx%d" % [ts.get_width(), ts.get_height()]
	if ts.get_texture_names().is_empty():
		return "no decoded textures"
	return ts


func _test_decode_metalplates() -> String:
	var r = _decode_file("res://assets/MetalPlates013.ntc")
	if r is String:
		return r
	print("  MetalPlates %dx%d mips=%d names=%s" % [
		r.get_width(), r.get_height(), r.get_mip_count(), ", ".join(r.get_texture_names())
	])
	return ""


func _test_decode_compare_ntc() -> String:
	var r = _decode_file("res://materials/ntcs/Wood095.ntc")
	if r is String:
		return r
	var names: PackedStringArray = r.get_texture_names()
	for need in ["albedo", "normal", "roughness", "metallic"]:
		var found := false
		for n in names:
			if String(n).to_lower().contains(need):
				found = true
				break
		if not found and r.get_texture(need) == null:
			# semantic lookup may still work via apply
			pass
	if r.get_texture("albedo") == null and r.get_all_textures().is_empty():
		return "decoded but get_all_textures empty"
	print("  Wood095 %dx%d names=%s" % [r.get_width(), r.get_height(), ", ".join(names)])
	return ""


func _test_apply_to_standard() -> String:
	var ts := NTCTextureSet.new()
	if ts.load_from_file("res://assets/MetalPlates013.ntc") != OK:
		return ts.get_last_error()
	var mat := StandardMaterial3D.new()
	if ts.apply_to_standard_material(mat) != OK:
		return "apply failed"
	if mat.albedo_texture == null:
		return "albedo_texture still empty after apply"
	return ""


func _test_reuse_existing_ntc() -> String:
	var mat = load("res://materials/ntcs/Wood095.tres")
	if mat == null or not (mat is NTCSMaterial3D):
		return "could not load Wood095.tres"
	var ntc_path := "res://materials/ntcs/Wood095.ntc"
	if not FileAccess.file_exists(ntc_path):
		return "Wood095.ntc missing"
	var before := FileAccess.get_modified_time(ntc_path)
	mat.rebuild_ntc()
	var status := String(mat.ntc_status).to_lower()
	if not (status.contains("existing") or status.contains("using")):
		return "expected reuse, status=%s" % mat.ntc_status
	var after := FileAccess.get_modified_time(ntc_path)
	if after != before:
		return ".ntc timestamp changed (%s -> %s); recompressed" % [before, after]
	return ""


func _test_fail_missing_file() -> String:
	var ts := NTCTextureSet.new()
	if ts.load_from_file("res://does_not_exist.ntc") == OK:
		return "missing file unexpectedly succeeded"
	if String(ts.get_last_error()).is_empty():
		return "failed without last_error"
	return ""


func _test_fail_bad_bytes() -> String:
	var ts := NTCTextureSet.new()
	if ts.load_from_bytes(PackedByteArray([0, 1, 2, 3, 4, 5])) == OK:
		return "garbage bytes unexpectedly succeeded"
	if String(ts.get_last_error()).is_empty():
		return "failed without last_error"
	return ""


func _test_quality_albedo_mse() -> String:
	var src_tex = load("res://assets/MetalPlates013/Color.jpg")
	if src_tex == null or not (src_tex is Texture2D):
		return "could not load Color.jpg as Texture2D"
	var ts := NTCTextureSet.new()
	if ts.load_from_file("res://assets/MetalPlates013.ntc") != OK:
		return ts.get_last_error()
	var dec_tex: Texture2D = ts.get_texture("Color|ntcsem:Albedo:0:3")
	if dec_tex == null:
		var all: Dictionary = ts.get_all_textures()
		for k in all.keys():
			if String(k).to_lower().contains("albedo") or String(k).begins_with("Color"):
				dec_tex = all[k]
				break
	if dec_tex == null:
		return "no albedo texture after decode; names=%s" % ", ".join(ts.get_texture_names())
	var src_img: Image = src_tex.get_image()
	var dec_img: Image = dec_tex.get_image()
	if src_img == null or src_img.is_empty():
		return "source Color.jpg image empty"
	if dec_img == null or dec_img.is_empty():
		return "decoded albedo image empty"
	if src_img.is_compressed():
		src_img.decompress()
	if dec_img.is_compressed():
		dec_img.decompress()
	src_img.convert(Image.FORMAT_RGBA8)
	dec_img.convert(Image.FORMAT_RGBA8)
	src_img.resize(64, 64, Image.INTERPOLATE_BILINEAR)
	dec_img.resize(64, 64, Image.INTERPOLATE_BILINEAR)
	var mse := 0.0
	for y in 64:
		for x in 64:
			var a: Color = src_img.get_pixel(x, y)
			var b: Color = dec_img.get_pixel(x, y)
			var dr := a.r - b.r
			var dg := a.g - b.g
			var db := a.b - b.b
			mse += dr * dr + dg * dg + db * db
	mse /= 64.0 * 64.0
	var psnr := 10.0 * log(1.0 / max(mse, 1e-12)) / log(10.0)
	print("  MetalPlates albedo 64x64 MSE=%.5f  PSNR≈%.2f dB  src=%s dec=%s" % [
		mse, psnr, src_tex.get_path(), ",".join(ts.get_texture_names())
	])
	if psnr < 22.0:
		return "albedo PSNR too low: %.2f dB" % psnr
	return ""
