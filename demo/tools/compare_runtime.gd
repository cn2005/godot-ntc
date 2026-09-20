extends Node3D

@export var is_ntc_pack := false

@onready var label: Label = $HUD/Label

var _yaw := 0.0
var _meshes: Array[MeshInstance3D] = []


func _ready() -> void:
	for child in get_children():
		if child is MeshInstance3D:
			_meshes.append(child)
			if child.material_override == null and child.get_surface_override_material_count() > 0:
				child.material_override = child.get_surface_override_material(0)
	if is_ntc_pack:
		_boot_ntc()
	else:
		_show_standard()


func _boot_ntc() -> void:
	var rt := NtcRuntime
	if not rt.initialize():
		label.text = "NTC init failed:\n%s" % rt.get_last_error()
		return
	var ok := 0
	var fail := []
	for mesh in _meshes:
		var mat = mesh.material_override
		if mat is NTCSMaterial3D:
			if mat.apply_decoded():
				ok += 1
			else:
				var err := "no set"
				if mat.ntc_texture_set:
					err = mat.ntc_texture_set.get_last_error()
				fail.append("%s: %s" % [mesh.name, err])
	var ver: Dictionary = rt.get_library_version()
	var lines := [
		"NTCS pack  |  LibNTC %s.%s.%s  |  GPU: %s" % [
			ver.get("major", 0), ver.get("minor", 0), ver.get("point", 0), rt.get_gpu_name()
		],
		"Decoded %d / %d materials from .ntc (on-load)." % [ok, _meshes.size()],
	]
	if fail:
		lines.append("Failures:\n" + "\n".join(fail))
	label.text = "\n".join(lines)


func _show_standard() -> void:
	label.text = "Standard pack  |  %d StandardMaterial3D spheres\nSource maps: albedo / normal / roughness / metallic / ao (Godot VRAM BCn import)." % _meshes.size()


func _process(delta: float) -> void:
	_yaw += delta * 0.35
	for mesh in _meshes:
		mesh.rotate_y(delta * 0.35)
