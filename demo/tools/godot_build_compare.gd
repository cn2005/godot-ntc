extends Node

## Build 16 Standard + 16 NTCS materials, compress each NTCS set inside
## NTCSMaterial3D (CUDA on-load path), then write the two compare scenes.
## This script never decodes .ntc — that happens when the compare scenes run.

var MATERIALS := [
	"Metal063", "CorrugatedSteel009", "Metal055A", "Metal046B",
	"Wood095", "WoodFloor051", "Rock064", "Bricks104",
	"Concrete034", "Tiles141", "Marble012", "Asphalt033",
	"PavingStones151", "Leather037", "Fabric061", "Onyx015",
]
const COLS := 4
const SPACING := 2.15
const BPP := 5.0
const STEPS := 2000

var _index := 0
var _current: NTCSMaterial3D
var _finishing := false
var _poll: Timer


func _ready() -> void:
	get_tree().auto_accept_quit = false
	get_tree().quit_on_go_back = false
	DirAccess.make_dir_recursive_absolute("res://materials/standard")
	DirAccess.make_dir_recursive_absolute("res://materials/ntcs")
	DirAccess.make_dir_recursive_absolute("res://scenes")
	_poll = Timer.new()
	_poll.wait_time = 1.0
	_poll.timeout.connect(_on_poll)
	add_child(_poll)
	var only := OS.get_cmdline_user_args()
	if only.size() > 0:
		MATERIALS.clear()
		for id in only:
			MATERIALS.append(id)
	print("Godot NTC compare: compressing %d materials via NTCSMaterial3D (bpp=%.1f, steps=%d)" % [
		MATERIALS.size(), BPP, STEPS
	])
	_start_next()


func _start_next() -> void:
	if _index >= MATERIALS.size():
		if OS.get_cmdline_user_args().is_empty():
			_write_scenes()
			print("Godot NTC compare: scenes written, quitting.")
		else:
			print("Godot NTC compare: single-material run done.")
		get_tree().quit(0)
		return
	var id: String = MATERIALS[_index]
	var maps := _load_maps(id)
	if maps.is_empty():
		push_error("Missing maps for %s" % id)
		get_tree().quit(1)
		return
	var std_path := "res://materials/standard/%s.tres" % id
	var err := ResourceSaver.save(_make_standard(maps), std_path)
	if err != OK:
		push_error("Save %s failed: %s" % [std_path, err])
		get_tree().quit(1)
		return

	var ntc_path := "res://materials/ntcs/%s.tres" % id
	var ntc_file := ntc_path.get_basename() + ".ntc"
	err = ResourceSaver.save(_make_ntcs(maps), ntc_path)
	if err != OK:
		push_error("Save %s failed: %s" % [ntc_path, err])
		get_tree().quit(1)
		return
	_current = load(ntc_path)
	if _current.ntc_file_written.is_connected(_on_ntc_written):
		_current.ntc_file_written.disconnect(_on_ntc_written)
	_current.ntc_file_written.connect(_on_ntc_written)
	print("Compressing %s  (%d/%d)  -> %s" % [id, _index + 1, MATERIALS.size(), ntc_path])

	if FileAccess.file_exists(ntc_file):
		var f := FileAccess.open(ntc_file, FileAccess.READ)
		var ntc_bytes := 0
		if f:
			ntc_bytes = f.get_length()
			f.close()
		if ntc_bytes > 1024 * 1024:
			var ts := NTCTextureSet.new()
			ts.file_path = ntc_file
			ts.semantic_map = {
				"albedo": "albedo",
				"normal": "normal",
				"roughness": "roughness",
				"metallic": "metallic",
				"ao": "ao",
			}
			ts.bits_per_pixel = BPP
			_current.ntc_texture_set = ts
			print("Existing .ntc for %s (%.1f MB), skipping CUDA" % [id, ntc_bytes / 1e6])
			_finish_current()
			return

	_current.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
	_current.rebuild_ntc()
	_poll.start()


func _on_poll() -> void:
	if _current == null:
		return
	var status := str(_current.ntc_status)
	if status.begins_with("Compress failed"):
		push_error(status)
		get_tree().quit(1)
		return
	if status.begins_with("Using existing") or (status.begins_with("Wrote ") and status.contains(".ntc")):
		_finish_current()


func _on_ntc_written(_path: String) -> void:
	_finish_current()


func _finish_current() -> void:
	if _finishing or _current == null:
		return
	_finishing = true
	_poll.stop()
	if _current.ntc_file_written.is_connected(_on_ntc_written):
		_current.ntc_file_written.disconnect(_on_ntc_written)
	var path := _current.resource_path
	var err := ResourceSaver.save(_current, path)
	if err != OK:
		push_error("Save NTCS after compress failed: %s" % err)
		get_tree().quit(1)
		return
	_patch_ntc_mode(path)
	print("Finished %s  status=%s" % [MATERIALS[_index], _current.ntc_status])
	_current = null
	_finishing = false
	_index += 1
	_start_next()


func _patch_ntc_mode(path: String) -> void:
	var txt := FileAccess.get_file_as_string(path)
	if txt.contains("ntc_mode = 1"):
		return
	if txt.contains("ntc_mode = 0"):
		txt = txt.replace("ntc_mode = 0", "ntc_mode = 1")
	elif txt.contains("[resource]"):
		txt = txt.replace("[resource]", "[resource]\nntc_mode = 1")
	var w := FileAccess.open(path, FileAccess.WRITE)
	if w:
		w.store_string(txt)


func _load_maps(id: String) -> Dictionary:
	var base := "res://assets/pbr4k/%s" % id
	var maps := {}
	for sem in ["albedo", "normal", "roughness", "metallic", "ao"]:
		var tex := _load_tex("%s/%s.jpg" % [base, sem])
		if tex == null:
			tex = _load_tex("%s/%s.png" % [base, sem])
		if tex:
			maps[sem] = tex
	for required in ["albedo", "normal", "roughness", "metallic"]:
		if not maps.has(required):
			push_error("%s missing %s" % [id, required])
			return {}
	return maps


func _load_tex(path: String) -> Texture2D:
	if not FileAccess.file_exists(path):
		return null
	var tex := load(path) as Texture2D
	if tex == null:
		return null
	var img: Image = tex.get_image()
	if img == null or img.is_empty():
		img = Image.new()
		if img.load(path) != OK:
			push_error("Cannot read pixels: %s" % path)
			return null
		return ImageTexture.create_from_image(img)
	return tex


func _make_standard(maps: Dictionary) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	_apply_maps(mat, maps)
	return mat


func _make_ntcs(maps: Dictionary) -> NTCSMaterial3D:
	var mat := NTCSMaterial3D.new()
	mat.ntc_bits_per_pixel = BPP
	mat.ntc_training_steps = STEPS
	_apply_maps(mat, maps)
	return mat


func _apply_maps(mat: BaseMaterial3D, maps: Dictionary) -> void:
	mat.albedo_texture = maps.albedo
	mat.metallic = 1.0
	mat.metallic_texture = maps.metallic
	mat.roughness_texture = maps.roughness
	mat.normal_enabled = true
	mat.normal_texture = maps.normal
	if maps.has("ao"):
		mat.ao_enabled = true
		mat.ao_texture = maps.ao


func _write_scenes() -> void:
	_write_scene_file("res://scenes/compare_standard.tscn", "materials/standard", false)
	_write_scene_file("res://scenes/compare_ntcs.tscn", "materials/ntcs", true)


func _write_scene_file(path: String, mat_dir: String, is_ntc: bool) -> void:
	var lines: PackedStringArray = []
	var load_steps := 3 + MATERIALS.size()
	lines.append("[gd_scene load_steps=%d format=3]" % load_steps)
	lines.append("")
	lines.append('[ext_resource type="Script" path="res://tools/compare_runtime.gd" id="1_script"]')
	for i in MATERIALS.size():
		lines.append('[ext_resource type="Material" path="res://%s/%s.tres" id="mat_%d"]' % [
			mat_dir, MATERIALS[i], i
		])
	lines.append("")
	lines.append('[sub_resource type="SphereMesh" id="Sphere"]')
	lines.append("radius = 0.72")
	lines.append("height = 1.44")
	lines.append("radial_segments = 48")
	lines.append("rings = 32")
	lines.append("")
	lines.append('[sub_resource type="Environment" id="Env"]')
	lines.append("background_mode = 1")
	lines.append("background_color = Color(0.08, 0.085, 0.1, 1)")
	lines.append("ambient_light_source = 1")
	lines.append("ambient_light_color = Color(0.35, 0.37, 0.4, 1)")
	lines.append("ambient_light_energy = 0.45")
	lines.append("tonemap_mode = 3")
	lines.append("")
	var root_name := "CompareNTCS" if is_ntc else "CompareStandard"
	lines.append('[node name="%s" type="Node3D"]' % root_name)
	lines.append('script = ExtResource("1_script")')
	lines.append("is_ntc_pack = %s" % ("true" if is_ntc else "false"))
	lines.append("")
	lines.append('[node name="Camera3D" type="Camera3D" parent="."]')
	lines.append("transform = Transform3D(1, 0, 0, 0, 0.86, 0.51, 0, -0.51, 0.86, 3.2, 3.4, 10.2)")
	lines.append("current = true")
	lines.append("")
	lines.append('[node name="DirectionalLight3D" type="DirectionalLight3D" parent="."]')
	lines.append("transform = Transform3D(0.81, -0.4, 0.42, 0, 0.74, 0.67, -0.58, -0.54, 0.6, 0, 4, 2)")
	lines.append("light_energy = 1.35")
	lines.append("shadow_enabled = true")
	lines.append("")
	lines.append('[node name="WorldEnvironment" type="WorldEnvironment" parent="."]')
	lines.append('environment = SubResource("Env")')
	lines.append("")
	lines.append('[node name="HUD" type="CanvasLayer" parent="."]')
	lines.append("")
	lines.append('[node name="Label" type="Label" parent="HUD"]')
	lines.append("offset_left = 16.0")
	lines.append("offset_top = 16.0")
	lines.append("offset_right = 1100.0")
	lines.append("offset_bottom = 180.0")
	lines.append("theme_override_font_sizes/font_size = 16")
	lines.append('text = "Loading..."')
	lines.append("")
	for i in MATERIALS.size():
		var x := (i % COLS) * SPACING
		var z := -int(i / COLS) * SPACING
		lines.append('[node name="Sphere_%02d" type="MeshInstance3D" parent="."]' % i)
		lines.append("transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, %s, 0.72, %s)" % [x, z])
		lines.append('mesh = SubResource("Sphere")')
		lines.append('surface_material_override/0 = ExtResource("mat_%d")' % i)
		lines.append("")
		lines.append('[node name="Name" type="Label3D" parent="Sphere_%02d"]' % i)
		lines.append('text = "%s"' % MATERIALS[i])
		lines.append("font_size = 48")
		lines.append("pixel_size = 0.004")
		lines.append("billboard = 1")
		lines.append("transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.95, 0)")
		lines.append("")
	var w := FileAccess.open(path, FileAccess.WRITE)
	if w == null:
		push_error("Cannot write %s" % path)
		return
	w.store_string("\n".join(lines) + "\n")
	print("Wrote %s" % path)
