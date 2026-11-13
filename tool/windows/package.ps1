$ErrorActionPreference = 'Stop'
$release = 'build/windows/x64/runner/Release'
foreach ($file in @('MusicCardX.exe', 'flutter_windows.dll', 'mcx_background.dll', 'data/icudtl.dat', 'data/flutter_assets/AssetManifest.bin')) {
  if (!(Test-Path "$release/$file")) { throw "Missing runtime file: $file" }
}
New-Item dist -ItemType Directory -Force | Out-Null
$bundle = 'dist/MusicCardX-Windows-x64'
New-Item $bundle -ItemType Directory -Force | Out-Null
Copy-Item "$release/*" $bundle -Recurse -Force
# Ship the redistributable DLLs app-local; no administrator installation required.
$vs = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath
$redist = Get-ChildItem "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$redist) { throw 'MSVC redistributable not found' }
Copy-Item "$($redist.FullName)/*.dll" $bundle -Force
foreach ($dll in @('vcruntime140.dll','msvcp140.dll')) { if (!(Test-Path "$bundle/$dll")) { throw "Missing $dll" } }
Copy-Item LICENSE,NOTICE $bundle
Copy-Item LICENSES "$bundle/LICENSES" -Recurse
Copy-Item assets/licenses "$bundle/third-party-licenses" -Recurse
Copy-Item docs/windows.md "$bundle/Windows说明.md"
if (Test-Path "$bundle/Config") { throw 'Developer configuration must not be packaged' }
$manifest = Get-ChildItem $bundle -Recurse -File | ForEach-Object {
  @{ path = [IO.Path]::GetRelativePath((Resolve-Path $bundle).Path,$_.FullName).Replace('\','/'); size=$_.Length; sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() }
} | Sort-Object { $_.path }
@{format='mcx-file-manifest';version=1;commit=$env:GITHUB_SHA;files=@($manifest)} | ConvertTo-Json -Depth 5 | Set-Content "$bundle/files.sha256.json" -Encoding utf8
Compress-Archive -Path "$bundle/*" -DestinationPath dist/MusicCardX-Windows-x64.zip -CompressionLevel Optimal
Remove-Item $bundle -Recurse -Force
& git archive --format=zip --output=dist/MusicCardX-Windows-source.zip HEAD
Get-ChildItem dist/*.zip | ForEach-Object { "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)" } | Set-Content dist/SHA256SUMS.txt -Encoding utf8
