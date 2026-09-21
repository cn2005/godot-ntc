# Builds the installable release ZIP for GitHub Releases and the Godot Asset Library.
#
# The ZIP is what users unzip on top of their own project, so it contains only
# what a project needs: addons/ntc (editor plugin) and bin (GDExtension + DLLs).
# Nothing is placed at the project root, where it could overwrite a user file.
param(
	[string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Addon = Join-Path $Root "demo\addons\ntc"
$Bin = Join-Path $Root "demo\bin\windows"
$Stage = Join-Path $Root ".cache\assetlib\NTC-OnLoad-$Version-windows"
$Zip = Join-Path $Root ".cache\assetlib\NTC-OnLoad-$Version-windows.zip"

foreach ($dll in @("libntc_godot.dll", "libntc.dll")) {
	if (-not (Test-Path (Join-Path $Bin $dll))) {
		throw "Build the extension first: $Bin\$dll is missing."
	}
}

Remove-Item $Stage, $Zip -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$Stage\addons\ntc", "$Stage\bin\windows" | Out-Null

# Editor plugin. Explicit list: no demo assets, docs, caches or build leftovers.
foreach ($name in @(
		"plugin.cfg",
		"ntc_plugin.gd", "ntc_plugin.gd.uid",
		"ntc_export_plugin.gd", "ntc_export_plugin.gd.uid",
		"README.md")) {
	Copy-Item (Join-Path $Addon $name) "$Stage\addons\ntc\" -Force
}

# Licences travel with the plugin folder, not the project root.
Copy-Item (Join-Path $Root "LICENSE") "$Stage\addons\ntc\" -Force
Copy-Item (Join-Path $Root "NOTICE.md") "$Stage\addons\ntc\" -Force
Copy-Item (Join-Path $Root "LICENSES\NVIDIA-RTX-SDKs.txt") "$Stage\addons\ntc\" -Force

# GDExtension entry point and native libraries.
Copy-Item (Join-Path $Root "demo\bin\ntc.gdextension") "$Stage\bin\" -Force
if (Test-Path (Join-Path $Root "demo\bin\ntc.gdextension.uid")) {
	Copy-Item (Join-Path $Root "demo\bin\ntc.gdextension.uid") "$Stage\bin\" -Force
}
foreach ($dll in @("libntc_godot.dll", "libntc.dll")) {
	# Byte copy: Copy-Item over a locked editor DLL can yield a partial file.
	[System.IO.File]::WriteAllBytes(
		(Join-Path "$Stage\bin\windows" $dll),
		[System.IO.File]::ReadAllBytes((Join-Path $Bin $dll)))
}

$leftovers = Get-ChildItem $Stage -Recurse -File |
	Where-Object { $_.Name -like "~*" -or $_.Extension -in ".pdb", ".exp", ".lib", ".import" }
if ($leftovers) {
	throw "Unexpected files staged: $($leftovers.FullName -join ', ')"
}

Compress-Archive -Path "$Stage\addons", "$Stage\bin" -DestinationPath $Zip -Force

Get-ChildItem $Stage -Recurse -File |
	ForEach-Object { "{0,10:N0}  {1}" -f $_.Length, $_.FullName.Substring($Stage.Length + 1) }
Write-Host ""
Get-Item $Zip | Select-Object FullName, Length
