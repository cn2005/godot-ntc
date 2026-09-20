extends Node3D

@onready var label: Label = $CanvasLayer/Label

var mesh: MeshInstance3D


func _ready() -> void:
	mesh = _find_mesh()
	if mesh == null:
		if label:
			label.text = "No MeshInstance3D in this scene."
		return

	var rt := NtcRuntime
	if not rt.initialize():
		label.text = "NTC init failed:\n%s" % rt.get_last_error()
		return

	var mat := mesh.material_override
	if mat == null and mesh.get_surface_override_material_count() > 0:
		mat = mesh.get_surface_override_material(0)
	if mat is NTCSMaterial3D:
		if not mat.apply_decoded():
			label.text = "ON_LOAD decode failed:\n%s" % (
				mat.ntc_texture_set.get_last_error() if mat.ntc_texture_set else "no NTCTextureSet"
			)
			return
		_show_status(rt, mat.ntc_texture_set)
		return

	var ts := NTCTextureSet.new()
	var err := ts.load_from_file("res://assets/MetalPlates013.ntc")
	if err != OK:
		label.text = "Decode failed:\n%s" % ts.get_last_error()
		return
	var fallback := NTCSMaterial3D.new()
	fallback.ntc_texture_set = ts
	fallback.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
	mesh.material_override = fallback
	_show_status(rt, ts)


func _find_mesh() -> MeshInstance3D:
	var named := get_node_or_null("MeshInstance3D")
	if named is MeshInstance3D:
		return named
	for child in get_children():
		if child is MeshInstance3D:
			return child
	return null


func _show_status(rt, ts) -> void:
	var ver: Dictionary = rt.get_library_version()
	label.text = "LibNTC %s.%s.%s  GPU: %s\n%s  %dx%d  mips=%d  %.2f bpp  weights=%s\ntextures: %s" % [
		ver.get("major", 0), ver.get("minor", 0), ver.get("point", 0),
		rt.get_gpu_name(),
		ts.get_file_path() if ts.get_file_path() != "" else "MetalPlates013.ntc",
		ts.get_width(), ts.get_height(), ts.get_mip_count(),
		ts.get_bits_per_pixel(), ts.get_weight_type(),
		", ".join(ts.get_texture_names())
	]


func _process(delta: float) -> void:
	if mesh:
		mesh.rotate_y(delta * 0.4)
