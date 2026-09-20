@tool
extends EditorExportPlugin

var _skip_paths: Dictionary = {}


func _get_name() -> String:
	return "ntc_onload"


func _get_customization_configuration_hash() -> int:
	return 3


func _export_begin(_features: PackedStringArray, _is_debug: bool, _path: String, _flags: int) -> void:
	_skip_paths.clear()


func _begin_customize_resources(_platform: EditorExportPlatform, features: PackedStringArray) -> bool:
	return "ntc_strip_sources" in features


func _customize_resource(resource: Resource, _path: String) -> Resource:
	if resource is NTCSMaterial3D and resource.ntc_mode == NTCSMaterial3D.NTC_MODE_ON_LOAD:
		return resource.create_export_copy()
	if resource is NTCTextureSet:
		return resource.create_export_copy()
	return null


func _export_file(path: String, _type: String, features: PackedStringArray) -> void:
	if path.get_file().begins_with("~"):
		skip()
		return
	if "ntc_strip_sources" in features and _is_source_texture_path(path):
		skip()
		return
	if _skip_paths.has(path):
		skip()
		return


func _is_source_texture_path(path: String) -> bool:
	var lower := path.to_lower().replace("\\", "/")
	if lower.ends_with(".ctex") or lower.ends_with(".jpg") or lower.ends_with(".jpeg") or lower.ends_with(".png"):
		return true
	if lower.contains("/assets/pbr4k/") or lower.contains("/.godot/imported/"):
		return true
	return false


func _collect(dir_path: String) -> void:
	var d := DirAccess.open(dir_path)
	if d == null:
		return
	d.list_dir_begin()
	var name := d.get_next()
	while name != "":
		if name.begins_with("."):
			name = d.get_next()
			continue
		var child := dir_path.path_join(name)
		if d.current_is_dir():
			_collect(child)
		elif name.get_extension() in ["tres", "res", "tscn", "scn"]:
			_inspect(load(child))
		name = d.get_next()
	d.list_dir_end()


func _inspect(res: Variant) -> void:
	if res == null:
		return
	if res is NTCSMaterial3D and res.ntc_mode == NTCSMaterial3D.NTC_MODE_ON_LOAD:
		for p in res.get_pack_excluded_paths():
			_skip_paths[p] = true
			_skip_paths[p + ".import"] = true
		return
	if res is PackedScene:
		var state: SceneState = res.get_state()
		for i in state.get_node_count():
			for j in state.get_node_property_count(i):
				_inspect(state.get_node_property_value(i, j))
		return
	if res is Array:
		for item in res:
			_inspect(item)
