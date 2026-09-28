# Package a locally verified SDK-enabled build without copying personal config
# or logs. Run tests before invoking this script; this script verifies contents.
param(
  [string]$BuildDir = "build",
  [string]$RuntimeDirectory = "out\sidecar",
  [string]$OutputDirectory = "out\release"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  $version = (Select-String CMakeLists.txt 'project\(.*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Matches[0].Groups[1].Value
  $cache = Get-Content (Join-Path $BuildDir 'CMakeCache.txt') -Raw
  if ($cache -notmatch '(?m)^SIDECAR_REQUIRE_SDKS:BOOL=ON\s*$') {
    throw 'Configure with SIDECAR_REQUIRE_SDKS=ON and rebuild before packaging.'
  }
  $stage = Join-Path $OutputDirectory "stage-v$version"
  $bundle = Join-Path $stage 'dlss5-wow-sidecar'
  if (Test-Path $stage) { throw "Staging directory already exists: $stage. Choose a fresh output directory." }
  New-Item -ItemType Directory -Path $bundle -Force | Out-Null
  foreach ($name in @('wowsidecar.exe', 'wowsidecar-manager.exe')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir "Release\$name") -Destination $bundle
  }
  foreach ($name in @('dxgi.dll', 'renodx-dlss5.addon64', 'nvngx_dlssnr.dll', 'nvngx_dlss.dll')) {
    Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $name) -Destination $bundle
  }
  $neuralHash = (Get-FileHash (Join-Path $bundle 'nvngx_dlssnr.dll') -Algorithm SHA256).Hash.ToLower()
  $manifest = Get-Content 'src\common\neural\RuntimeManifest.cpp' -Raw
  if (-not $manifest.Contains($neuralHash)) { throw 'Neural runtime is not recognized by the project manifest.' }
  Copy-Item LICENSE, THIRD-PARTY-NOTICES.md, README.md, README.ru.md, CHANGELOG.md, CHANGELOG.ru.md -Destination $bundle
  Copy-Item docs -Destination $bundle -Recurse
  New-Item -ItemType Directory -Path (Join-Path $bundle 'assets\art'), (Join-Path $bundle 'assets\fonts') -Force | Out-Null
  Copy-Item assets\art\PROVENANCE.md -Destination (Join-Path $bundle 'assets\art')
  Copy-Item assets\fonts\OFL-*.txt -Destination (Join-Path $bundle 'assets\fonts')
  $entries = Get-ChildItem $bundle -File | ForEach-Object {
    [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() }
  }
  $commit = git -c "safe.directory=$($root.Replace('\','/'))" rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Could not read source commit.' }
  [ordered]@{ version = $version; sourceCommit = $commit; neuralRuntimeSha256 = $neuralHash; files = @($entries) } |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $bundle 'BUILD-MANIFEST.json') -Encoding utf8
  $zip = Join-Path $OutputDirectory "dlss5-wow-sidecar-v$version.zip"
  if (Test-Path $zip) { throw "Archive already exists: $zip" }
  Compress-Archive -LiteralPath $bundle -DestinationPath $zip -CompressionLevel Optimal
  $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
  "$hash  $(Split-Path $zip -Leaf)" | Set-Content (Join-Path $OutputDirectory "SHA256SUMS-v$version.txt") -Encoding ascii
  Write-Output "Packaged $zip"
  Write-Output "SHA256 $hash"
} finally {
  Pop-Location
}
