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
  $version = (Select-String -Path CMakeLists.txt -Pattern 'project\(.*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Matches[0].Groups[1].Value
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
  foreach ($name in @('dxgi.dll', 'renodx-dlss5.addon64', 'nvngx_dlss.dll')) {
    Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $name) -Destination $bundle
  }
  # Both neural runtimes ship in their slots; the overlay copies the one the
  # card needs next to itself at launch, so no loose nvngx_dlssnr.dll is shipped.
  # Each has to be a build the manifest knows, and the variant its slot implies.
  $manifest = Get-Content 'src\common\neural\RuntimeManifest.cpp' -Raw
  $neuralHashes = [ordered]@{}
  foreach ($slot in @(@{ name = 'latest'; variant = 'Stock' }, @{ name = 'compatible'; variant = 'AdaPatched' })) {
    $relative = "runtimes\$($slot.name)\nvngx_dlssnr.dll"
    $source = Join-Path $RuntimeDirectory $relative
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing neural runtime: $source" }
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLower()
    if ($manifest -notmatch "(?s)`"$hash`",\s*`"[0-9.]+`",\s*RuntimeVariant::$($slot.variant)\}") {
      throw "$relative is not a $($slot.variant) build in the project manifest."
    }
    $destination = Join-Path $bundle $relative
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    $neuralHashes[$slot.name] = $hash
  }
  Copy-Item LICENSE, THIRD-PARTY-NOTICES.md, README.md, README.ru.md, CHANGELOG.md, CHANGELOG.ru.md -Destination $bundle
  Copy-Item docs -Destination $bundle -Recurse
  New-Item -ItemType Directory -Path (Join-Path $bundle 'assets\art'), (Join-Path $bundle 'assets\fonts') -Force | Out-Null
  Copy-Item assets\art\PROVENANCE.md -Destination (Join-Path $bundle 'assets\art')
  Copy-Item assets\fonts\OFL-*.txt -Destination (Join-Path $bundle 'assets\fonts')
  $entries = @(Get-ChildItem $bundle -File) + @(Get-ChildItem (Join-Path $bundle 'runtimes') -File -Recurse) | ForEach-Object {
    $name = $_.FullName.Substring((Resolve-Path $bundle).Path.Length + 1)
    [ordered]@{ name = $name; bytes = $_.Length; sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower() }
  }
  $commit = git -c "safe.directory=$($root.Replace('\','/'))" rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Could not read source commit.' }
  [ordered]@{ version = $version; sourceCommit = $commit; neuralRuntimeSha256 = $neuralHashes; files = @($entries) } |
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
