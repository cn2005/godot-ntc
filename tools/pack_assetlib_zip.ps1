param(
	[string]$Version = "0.1.0"
)

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Stage = Join-Path $Root ".cache\assetlib\NTC-OnLoad-$Version-windows"
$Zip = Join-Path $Root ".cache\assetlib\NTC-OnLoad-$Version-windows.zip"
$Bin = Join-Path $Root "demo\bin\windows"

if (-not (Test-Path (Join-Path $Bin "libntc_godot.dll"))) {
	throw "Build the extension first: $Bin\libntc_godot.dll is missing."
}

Remove-Item $Stage, $Zip -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$Stage\addons\ntc","$Stage\bin\windows" | Out-Null

Copy-Item "$Root\demo\addons\ntc\*" "$Stage\addons\ntc\" -Recurse -Force
Copy-Item "$Root\demo\bin\ntc.gdextension" "$Stage\bin\" -Force
foreach ($dll in @("libntc_godot.dll", "libntc.dll")) {
	[System.IO.File]::WriteAllBytes(
		(Join-Path "$Stage\bin\windows" $dll),
		[System.IO.File]::ReadAllBytes((Join-Path $Bin $dll))
	)
}
Copy-Item "$Root\LICENSE","$Root\NOTICE.md","$Root\README.md" "$Stage\" -Force
Copy-Item "$Root\LICENSE","$Root\NOTICE.md" "$Stage\addons\ntc\" -Force

Compress-Archive -Path "$Stage\*" -DestinationPath $Zip -Force
Get-Item $Zip | Select-Object FullName, Length
