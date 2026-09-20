@tool
extends EditorPlugin

var _export_plugin: EditorExportPlugin
var _debounce: SceneTreeTimer


func _enter_tree() -> void:
	_export_plugin = preload("ntc_export_plugin.gd").new()
	add_export_plugin(_export_plugin)
	var inspector := get_editor_interface().get_inspector()
	if inspector and not inspector.property_edited.is_connected(_on_property_edited):
		inspector.property_edited.connect(_on_property_edited)


func _exit_tree() -> void:
	var inspector := get_editor_interface().get_inspector()
	if inspector and inspector.property_edited.is_connected(_on_property_edited):
		inspector.property_edited.disconnect(_on_property_edited)
	if _export_plugin:
		remove_export_plugin(_export_plugin)
		_export_plugin = null


func _on_property_edited(property: String) -> void:
	var obj = get_editor_interface().get_inspector().get_edited_object()
	if obj == null or not obj is NTCSMaterial3D:
		return
	if obj.ntc_mode != NTCSMaterial3D.NTC_MODE_ON_LOAD:
		return
	if property == "ntc_mode":
		return
	if not (property.ends_with("texture") or property in ["ntc_bits_per_pixel", "ntc_training_steps"]):
		return
	if _debounce and _debounce.timeout.is_connected(_flush_sources):
		_debounce.timeout.disconnect(_flush_sources)
	_debounce = get_tree().create_timer(0.8)
	_debounce.timeout.connect(_flush_sources.bind(obj), CONNECT_ONE_SHOT)


func _flush_sources(obj: Object) -> void:
	if obj and obj is NTCSMaterial3D:
		obj.notify_sources_changed()
