extends SceneTree

const MATERIALS := [
	"Metal063", "CorrugatedSteel009", "Metal055A", "Metal046B",
	"Wood095", "WoodFloor051", "Rock064", "Bricks104",
	"Concrete034", "Tiles141", "Marble012", "Asphalt033",
	"PavingStones151", "Leather037", "Fabric061", "Onyx015",
]

const COLS := 4
const SPACING := 2.15


func _init() -> void:
	var err := _build()
	if err != OK:
		push_error("build_compare_scenes failed: %s" % err)
		quit(1)
		return
	print("compare scenes written")
	quit(0)


func _build() -> Error:
	DirAccess.make_dir_recursive_absolute("res://materials/standard")
	DirAccess.make_dir_recursive_absolute("res://materials/ntcs")
	DirAccess.make_dir_recursive_absolute("res://scenes")
	var std_mats: Array[StandardMaterial3D] = []
	var ntc_mats: Array[NTCSMaterial3D] = []
	for id in MATERIALS:
		var base := "res://assets/pbr4k/%s" % id
		var albedo := _load_tex("%s/albedo.jpg" % base)
		if albedo == null:
			albedo = _load_tex("%s/albedo.png" % base)
		var normal := _load_tex("%s/normal.jpg" % base)
		var rough := _load_tex("%s/roughness.jpg" % base)
		var metal := _load_tex("%s/metallic.jpg" % base)
		var ao := _load_tex("%s/ao.jpg" % base)
		if albedo == null or normal == null or rough == null or metal == null:
			push_error("Missing required maps for %s" % id)
			return ERR_FILE_NOT_FOUND
		var std := _make_standard(albedo, normal, rough, metal, ao)
		var std_path := "res://materials/standard/%s.tres" % id
		var save_err := ResourceSaver.save(std, std_path)
		if save_err != OK:
			push_error("Save %s failed: %s" % [std_path, save_err])
			return save_err
		std_mats.append(load(std_path))
		var ntc_path := "%s/%s.ntc" % [base, id]
		if not FileAccess.file_exists(ntc_path):
			push_error("Missing %s" % ntc_path)
			return ERR_FILE_NOT_FOUND
		var ntc := _make_ntcs(ntc_path)
		var ntc_res := "res://materials/ntcs/%s.tres" % id
		save_err = ResourceSaver.save(ntc, ntc_res)
		if save_err != OK:
			push_error("Save %s failed: %s" % [ntc_res, save_err])
			return save_err
		ntc_mats.append(load(ntc_res))
		print("materials %s" % id)
	_write_scene("res://scenes/compare_standard.tscn", std_mats, false)
	_write_scene("res://scenes/compare_ntcs.tscn", ntc_mats, true)
	return OK


func _load_tex(path: String) -> Texture2D:
	if not FileAccess.file_exists(path):
		return null
	return load(path) as Texture2D


func _make_standard(albedo: Texture2D, normal: Texture2D, rough: Texture2D, metal: Texture2D, ao: Texture2D) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_texture = albedo
	mat.metallic = 1.0
	mat.metallic_texture = metal
	mat.roughness_texture = rough
	mat.normal_enabled = true
	mat.normal_texture = normal
	if ao:
		mat.ao_enabled = true
		mat.ao_texture = ao
	return mat


func _make_ntcs(ntc_path: String) -> NTCSMaterial3D:
	var ts := NTCTextureSet.new()
	ts.file_path = ntc_path
	ts.semantic_map = {
		"albedo": "albedo",
		"normal": "normal",
		"roughness": "roughness",
		"metallic": "metallic",
		"ao": "ao",
	}
	ts.bits_per_pixel = 5.0
	var mat := NTCSMaterial3D.new()
	mat.ntc_texture_set = ts
	mat.ntc_mode = NTCSMaterial3D.NTC_MODE_ON_LOAD
	mat.metallic = 1.0
	return mat


func _write_scene(path: String, mats: Array, is_ntc: bool) -> void:
	var root := Node3D.new()
	root.name = "CompareNTCS" if is_ntc else "CompareStandard"
	root.set_script(load("res://tools/compare_runtime.gd"))
	root.set("is_ntc_pack", is_ntc)

	var cam := Camera3D.new()
	cam.name = "Camera3D"
	cam.current = true
	cam.position = Vector3(3.2, 3.4, 10.2)
	cam.look_at(Vector3(3.2, 0.7, 0.0))
	root.add_child(cam)
	cam.owner = root

	var light := DirectionalLight3D.new()
	light.name = "DirectionalLight3D"
	light.rotation_degrees = Vector3(-42, -35, 0)
	light.shadow_enabled = true
	light.light_energy = 1.35
	root.add_child(light)
	light.owner = root

	var env := WorldEnvironment.new()
	env.name = "WorldEnvironment"
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.08, 0.085, 0.1)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.35, 0.37, 0.4)
	environment.ambient_light_energy = 0.45
	environment.tonemap_mode = Environment.TONE_MAPPER_ACES
	env.environment = environment
	root.add_child(env)
	env.owner = root

	var layer := CanvasLayer.new()
	layer.name = "HUD"
	root.add_child(layer)
	layer.owner = root
	var label := Label.new()
	label.name = "Label"
	label.position = Vector2(16, 16)
	label.size = Vector2(1100, 180)
	label.add_theme_font_size_override("font_size", 16)
	label.text = "Loading..."
	layer.add_child(label)
	label.owner = root

	var sphere := SphereMesh.new()
	sphere.radius = 0.72
	sphere.height = 1.44
	sphere.radial_segments = 48
	sphere.rings = 32

	for i in mats.size():
		var x := i % COLS
		var z := int(i / COLS)
		var mesh := MeshInstance3D.new()
		mesh.name = "Sphere_%02d" % i
		mesh.mesh = sphere
		mesh.position = Vector3(x * SPACING, 0.72, -z * SPACING)
		mesh.material_override = mats[i]
		root.add_child(mesh)
		mesh.owner = root
		var tag := Label3D.new()
		tag.name = "Name"
		tag.text = MATERIALS[i]
		tag.font_size = 48
		tag.pixel_size = 0.004
		tag.position = Vector3(0, -0.95, 0)
		tag.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		mesh.add_child(tag)
		tag.owner = root

	var packed := PackedScene.new()
	packed.pack(root)
	ResourceSaver.save(packed, path)
	root.free()
