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
# Keep the full file manifest, including the runtime DLL hash, in the app package.
$runtime = 'dist/flutter-runtime'
New-Item $runtime -ItemType Directory -Force | Out-Null
Move-Item "$bundle/flutter_windows.dll" "$runtime/flutter_windows.dll"
$engineHash = (Get-FileHash "$runtime/flutter_windows.dll" -Algorithm SHA256).Hash.ToLower()
@{flutter='3.47.9';dart='3.13.7';platform='windows-x64';sha256=$engineHash;commit=$env:GITHUB_SHA} | ConvertTo-Json | Set-Content "$runtime/flutter-runtime.json" -Encoding utf8
if (!$env:FLUTTER_ROOT -or !(Test-Path "$env:FLUTTER_ROOT/LICENSE")) { throw 'Flutter license not found' }
Copy-Item "$env:FLUTTER_ROOT/LICENSE" "$runtime/LICENSE.flutter.txt"
@{flutter='3.47.9';dart='3.13.7';platform='windows-x64';sha256=$engineHash;file='flutter_windows.dll'} | ConvertTo-Json | Set-Content "$bundle/required-flutter-runtime.json" -Encoding utf8
if (Test-Path "$bundle/flutter_windows.dll") { throw 'Flutter DLL must be excluded from app package' }
Compress-Archive -Path "$bundle/*" -DestinationPath dist/MusicCardX-Windows-app-x64.zip -CompressionLevel Optimal
Compress-Archive -Path "$runtime/*" -DestinationPath dist/MusicCardX-Flutter-runtime-x64.zip -CompressionLevel Optimal
Remove-Item $runtime -Recurse -Force
Remove-Item $bundle -Recurse -Force
& git archive --format=zip --output=dist/MusicCardX-Windows-source.zip HEAD
Get-ChildItem dist/*.zip | ForEach-Object { "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $($_.Name)" } | Set-Content dist/SHA256SUMS.txt -Encoding utf8
