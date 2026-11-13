$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class McxWindow {
 public delegate bool EnumProc(IntPtr h,IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$temp=Join-Path $env:RUNNER_TEMP ([guid]::NewGuid().ToString())
New-Item $temp -ItemType Directory | Out-Null
$process=$null
try {
 Expand-Archive dist/MusicCardX-Windows-x64.zip $temp
 $env:MCX_SMOKE_REPORT=Join-Path $temp 'ready.txt'
 $process=Start-Process "$temp/MusicCardX.exe" -WorkingDirectory $env:RUNNER_TEMP -PassThru
 $deadline=(Get-Date).AddSeconds(60)
 while (!(Test-Path $env:MCX_SMOKE_REPORT)) {
  if($process.HasExited) { throw "Application exited before editor readiness: $($process.ExitCode)" }
  if((Get-Date) -gt $deadline) { throw 'Editor failed to initialize within 60 seconds' }
  Start-Sleep -Milliseconds 250
 }
 $script:handle=[IntPtr]::Zero
 [McxWindow]::EnumWindows({param($h,$l)
  [uint32]$pidValue=0
  [void][McxWindow]::GetWindowThreadProcessId($h,[ref]$pidValue)
  $name=New-Object Text.StringBuilder 128
  [void][McxWindow]::GetClassName($h,$name,128)
  if($pidValue -eq $process.Id -and $name.ToString() -eq 'MusicCardXDesktop') { $script:handle=$h }
  return $true
 },[IntPtr]::Zero) | Out-Null
 if($script:handle -eq [IntPtr]::Zero -or ![McxWindow]::IsWindowVisible($script:handle)) { throw 'Main window not visible' }
 [void][McxWindow]::PostMessage($script:handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 if(!$process.WaitForExit(8000)) { throw 'Window close stalled' }
 if($process.ExitCode -ne 0) { throw "Application exited with code $($process.ExitCode)" }
 'Editor initialized and native main window closed successfully.'
} finally {
 if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
 Remove-Item Env:MCX_SMOKE_REPORT -ErrorAction SilentlyContinue
 Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}
