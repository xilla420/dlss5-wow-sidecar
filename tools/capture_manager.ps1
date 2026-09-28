# Captures the manager window in each theme (and optionally a language) for
# the README and for review. Runs against out\sidecar, the real run directory,
# and restores its sidecar.toml afterwards.
#
#   powershell -File tools\capture_manager.ps1 -OutDir docs\screenshots
#   powershell -File tools\capture_manager.ps1 -OutDir x -Themes questlog -Languages ja -Pages Tuning
param(
  [string]$OutDir = "docs\screenshots",
  [string[]]$Themes = @("stormwind", "questlog", "dragonflight"),
  [string[]]$Languages = @("en"),
  [string[]]$Pages = @("Status"),
  [string]$RunDir = "out\sidecar",
  [int]$Width = 1280,
  [int]$Height = 860,
  # Extra top-level settings for the run, e.g. "neural_passes = 2".
  [string[]]$Set = @()
)
$ErrorActionPreference = "Stop"
# `-File` hands a comma list over as one string; split it here.
$Themes = $Themes -split ','
$Languages = $Languages -split ','
$Pages = $Pages -split ','
$Set = @($Set | Where-Object { $_ } | ForEach-Object { $_ -split ';' })
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
'@
[Win]::SetProcessDPIAware() | Out-Null

$toml = Join-Path $RunDir "sidecar.toml"
$backup = "$toml.capture-backup"
Copy-Item $toml $backup -Force
New-Item -ItemType Directory -Force $OutDir | Out-Null
try {
  foreach ($lang in $Languages) {
    foreach ($theme in $Themes) {
      foreach ($page in $Pages) {
        $text = Get-Content $backup -Raw
        $text = $text -replace '(?m)^(theme|language) = .*\r?\n', ''
        # Inserted before the first key, so they land at the top level rather
        # than inside [neural] or [hotkeys].
        $extra = ""
        foreach ($line in $Set) {
          $key = ($line -split '=')[0].Trim()
          $text = $text -replace ('(?m)^' + [regex]::Escape($key) + ' = .*\r?\n'), ''
          $extra += "$line`r`n"
        }
        $text = ([regex]'(?m)^(?=[a-z_]+ = )').Replace($text, "language = `"$lang`"`r`ntheme = `"$theme`"`r`n$extra", 1)
        [IO.File]::WriteAllText((Resolve-Path $toml), $text, (New-Object Text.UTF8Encoding $false))
        $p = Start-Process (Join-Path $RunDir "wowsidecar-manager.exe") -ArgumentList $page -WorkingDirectory $RunDir -PassThru
        $h = [IntPtr]::Zero
        for ($i = 0; $i -lt 50 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 200; $p.Refresh(); $h = $p.MainWindowHandle }
        [Win]::MoveWindow($h, 40, 40, $Width, $Height, $true) | Out-Null
        [Win]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 2500
        $r = New-Object Win+RECT
        [Win]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        # PrintWindow asks the manager for its own pixels. Never a copy of the
        # screen: whatever else is open on the desktop could be on top of the
        # window, and a screen grab would publish it.
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $dc = $g.GetHdc()
        $ok = [Win]::PrintWindow($h, $dc, 2)   # PW_RENDERFULLCONTENT
        $g.ReleaseHdc($dc)
        if (-not $ok) { throw "PrintWindow failed for the manager window" }
        # Trim the invisible resize border Windows 11 keeps round the frame.
        $inner = $bmp.Clone((New-Object System.Drawing.Rectangle 8, 0, ($w - 16), ($hh - 8)), $bmp.PixelFormat)
        $g.Dispose(); $bmp.Dispose()
        $bmp = $inner
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $name = if ($Languages.Count -gt 1) { "manager-$theme-$lang-$($page.ToLower()).png" } else { "manager-$theme-$($page.ToLower()).png" }
        $bmp.Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Stop-Process -Id $p.Id -Force
        Start-Sleep -Milliseconds 300
        Write-Output "captured $name"
      }
    }
  }
} finally {
  Move-Item $backup $toml -Force
}
