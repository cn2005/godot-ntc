param(
	[string]$Godot = "$env:USERPROFILE\Desktop\Godot_v4.7.2-stable_win64_console.exe"
)

$ErrorActionPreference = "Continue"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Demo = Join-Path $Root "demo"
$OutDir = Join-Path $Root ".cache\release_tests"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Report = Join-Path $OutDir "RESULTS.md"
$script:Pass = 0
$script:Fail = 0
$script:Lines = @("# Godot NTC release tests", "", "Godot: ``$Godot``", "Time: $(Get-Date -Format o)", "")

function Add-Result([string]$name, [bool]$ok, [string]$detail) {
	if ($ok) { $script:Pass++; $mark = "PASS" } else { $script:Fail++; $mark = "FAIL" }
	$script:Lines += "- **$mark** ``$name``  $detail"
	Write-Host "$mark  $name  $detail"
}

if (-not (Test-Path $Godot)) { throw "Godot CLI not found: $Godot" }

# 1. Extension load
$loadLog = Join-Path $OutDir "01_load.log"
cmd /c "`"$Godot`" --headless --path `"$Demo`" --quit-after 2 > `"$loadLog`" 2>&1"
$loadTxt = Get-Content $loadLog -Raw -ErrorAction SilentlyContinue
$loadOk = ($loadTxt -notmatch "Can't open GDExtension") -and ($loadTxt -notmatch "Can't open dynamic library")
Add-Result "extension_load" $loadOk $(if ($loadOk) { "GDExtension loaded" } else { "see 01_load.log" })

# 2-7 + quality: in-engine script
$apiLog = Join-Path $OutDir "02_api.log"
cmd /c "`"$Godot`" --headless --path `"$Demo`" -s res://tools/run_release_tests.gd > `"$apiLog`" 2>&1"
$apiCode = $LASTEXITCODE
$apiTxt = Get-Content $apiLog -Raw -ErrorAction SilentlyContinue
$apiOk = ($apiCode -eq 0) -and ($apiTxt -match "passed") -and ($apiTxt -notmatch "(?m)^FAIL")
$summary = ($apiTxt -split "`n" | Where-Object { $_ -match "^(PASS|FAIL|====)" }) -join " | "
Add-Result "in_engine_suite" $apiOk "exit=$apiCode  $summary"

# 3. Opening existing ON_LOAD materials must not start CUDA
$reuseLog = Join-Path $OutDir "03_reuse.log"
cmd /c "`"$Godot`" --headless --path `"$Demo`" --quit-after 6 > `"$reuseLog`" 2>&1"
$reuseTxt = Get-Content $reuseLog -Raw -ErrorAction SilentlyContinue
$recompressed = $reuseTxt -match "BeginCompression|Training "
Add-Result "no_recompress_on_open" (-not $recompressed) $(if ($recompressed) { "CUDA training started on open" } else { "no BeginCompression/Training" })

# 4. Existing official export pack
$pck = "F:\work\NTC\dist\compare\ntcs\NTCCompare.pck"
$exe = "F:\work\NTC\dist\compare\ntcs\NTCCompare.exe"
if (Test-Path $pck) {
	$pckOut = Join-Path $OutDir "04_pck.txt"
	python "F:\work\NTC\dist\compare\list_pck.py" $pck | Out-File -Encoding utf8 $pckOut
	$pckTxt = Get-Content $pckOut -Raw
	$hasNtc = $pckTxt -match "\bntc\b"
	$hasCtex = $pckTxt -match "ctex"
	$exeOk = (Test-Path $exe) -and ((Get-Item $exe).Length -lt 150MB)
	Add-Result "export_pck_has_ntc" $hasNtc $(if ($hasNtc) { "ntc payload present" } else { $pckTxt.Substring(0, [Math]::Min(200, $pckTxt.Length)) })
	Add-Result "export_pck_no_ctex" (-not $hasCtex) $(if ($hasCtex) { "ctex still packed" } else { "no .ctex in NTCS pack" })
	Add-Result "export_exe_is_template" $exeOk $(if ($exeOk) { "exe $([math]::Round((Get-Item $exe).Length/1MB,2)) MB" } else { "exe missing or looks like editor" })
} else {
	Add-Result "export_pck_has_ntc" $false "NTCCompare.pck missing"
}

# 5. Size numbers
$ntcSum = (Get-ChildItem "$Demo\materials\ntcs\*.ntc" -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
$srcSum = (Get-ChildItem "$Demo\assets\pbr4k" -Recurse -Include *.jpg,*.png -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
$stdPck = "F:\work\NTC\dist\compare\standard\StandardCompare.pck"
$ntcPckSize = if (Test-Path $pck) { (Get-Item $pck).Length } else { 0 }
$stdPckSize = if (Test-Path $stdPck) { (Get-Item $stdPck).Length } else { 0 }
$sizeOk = ($ntcSum -gt 0) -and ($stdPckSize -gt $ntcPckSize * 3)
Add-Result "pack_size_ratio" $sizeOk ("ntc files={0:N2} MB  NTCS pck={1:N2} MB  Standard pck={2:N2} MB  src={3:N2} MB" -f ($ntcSum/1MB), ($ntcPckSize/1MB), ($stdPckSize/1MB), ($srcSum/1MB))

# 6. Exported exe smoke (template, not editor)
$smokeLog = Join-Path $OutDir "06_ntcs_exe.log"
$smokeErr = Join-Path $OutDir "06_ntcs_exe.err.log"
if (Test-Path $exe) {
	$p = Start-Process -FilePath $exe -ArgumentList "--headless","--quit-after","8" -WorkingDirectory (Split-Path $exe) -PassThru -RedirectStandardOutput $smokeLog -RedirectStandardError $smokeErr
	if (-not $p.WaitForExit(45000)) { Stop-Process -Id $p.Id -Force }
	$err = Get-Content $smokeErr -Raw -ErrorAction SilentlyContinue
	$fatal = $err -match "Failed loading scene|Can't open GDExtension|Can't open dynamic library"
	Add-Result "exported_exe_launch" (-not $fatal) $(if ($fatal) { "fatal in 06_ntcs_exe.err.log" } else { "scene/extension loaded" })
} else {
	Add-Result "exported_exe_launch" $false "NTCCompare.exe missing"
}

# 8. Clean project: drop only the extension + one .ntc, import, then decode.
$clean = Join-Path $OutDir "clean_project"
Remove-Item $clean -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$clean\bin\windows","$clean\assets" | Out-Null
Copy-Item "$Demo\bin\ntc.gdextension" "$clean\bin\" -Force
if (Test-Path "$Demo\bin\ntc.gdextension.uid") {
	Copy-Item "$Demo\bin\ntc.gdextension.uid" "$clean\bin\" -Force
}
Copy-Item "$Demo\bin\windows\libntc.dll" "$clean\bin\windows\" -Force
Copy-Item "$Demo\bin\windows\libntc_godot.dll" "$clean\bin\windows\" -Force
Copy-Item "$Demo\assets\MetalPlates013.ntc" "$clean\assets\" -Force
@'
extends Node
func _ready() -> void:
	if not Engine.has_singleton("NtcRuntime"):
		print("CLEAN_FAIL no_singleton")
		get_tree().quit(1)
		return
	var rt = Engine.get_singleton("NtcRuntime")
	if rt == null or not rt.initialize():
		print("CLEAN_FAIL init")
		get_tree().quit(1)
		return
	if not ClassDB.class_exists("NTCTextureSet"):
		print("CLEAN_FAIL no_class")
		get_tree().quit(1)
		return
	var ts = ClassDB.instantiate("NTCTextureSet")
	if ts == null or ts.load_from_file("res://assets/MetalPlates013.ntc") != OK:
		print("CLEAN_FAIL decode")
		get_tree().quit(1)
		return
	print("CLEAN_OK %dx%d %s" % [ts.get_width(), ts.get_height(), rt.get_gpu_name()])
	get_tree().quit(0)
'@ | Set-Content -Encoding ASCII (Join-Path $clean "smoke.gd")
@'
[gd_scene load_steps=2 format=3]
[ext_resource type="Script" path="res://smoke.gd" id="1"]
[node name="Smoke" type="Node"]
script = ExtResource("1")
'@ | Set-Content -Encoding ASCII (Join-Path $clean "smoke.tscn")
@"
; Engine configuration file.
config_version=5
[application]
config/name="NTC Clean Smoke"
run/main_scene="res://smoke.tscn"
config/features=PackedStringArray("4.7", "Forward Plus")
"@ | Set-Content -Encoding ASCII (Join-Path $clean "project.godot")
$importLog = Join-Path $OutDir "08_clean_import.log"
$cleanLog = Join-Path $OutDir "08_clean.log"
cmd /c "`"$Godot`" --headless --path `"$clean`" --import --quit > `"$importLog`" 2>&1"
cmd /c "`"$Godot`" --headless --path `"$clean`" --quit-after 20 > `"$cleanLog`" 2>&1"
$cleanTxt = Get-Content $cleanLog -Raw -ErrorAction SilentlyContinue
Add-Result "clean_project_decode" ($cleanTxt -match "CLEAN_OK") $(if ($cleanTxt -match "CLEAN_OK") { ($cleanTxt -split "`n" | Where-Object { $_ -match "CLEAN_" }) -join " " } else { "see 08_clean.log" })

$script:Lines += ""
$script:Lines += "**Total: $Pass passed, $Fail failed.**"
$script:Lines | Set-Content -Encoding utf8 $Report
Write-Host ""
Write-Host "TOTAL  $Pass passed, $Fail failed"
Write-Host "Report $Report"
if ($Fail -gt 0) { exit 1 }
exit 0
