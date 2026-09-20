extends SceneTree

## Headless entry that keeps the main loop alive until NTCS compression finishes.

func _initialize() -> void:
	var node := preload("res://tools/godot_build_compare.gd").new()
	node.name = "GodotBuildCompare"
	root.add_child(node)
