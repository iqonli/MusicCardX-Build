$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class McxWindow {
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int cx,int cy,uint flags);
 [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect rect);
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
 Expand-Archive dist/MusicCardX-Windows-app-x64.zip $temp
 if(Test-Path "$temp/flutter_windows.dll") { throw 'APP package unexpectedly contains Flutter DLL' }
 Expand-Archive dist/MusicCardX-Flutter-runtime-x64.zip $temp
 $requirement=Get-Content "$temp/required-flutter-runtime.json" -Raw | ConvertFrom-Json
 $hash=(Get-FileHash "$temp/flutter_windows.dll" -Algorithm SHA256).Hash.ToLower()
 if($hash -ne $requirement.sha256) { throw 'Flutter runtime does not match APP package' }
 $env:MCX_SMOKE_REPORT=Join-Path $temp 'ready.txt'
 New-Item diagnostics -ItemType Directory -Force | Out-Null
 $process=Start-Process "$temp/MusicCardX.exe" -WorkingDirectory $env:RUNNER_TEMP -RedirectStandardOutput "$PWD/diagnostics/stdout.txt" -RedirectStandardError "$PWD/diagnostics/stderr.txt" -PassThru
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
 # Exercise window resizing and rapid wheel traffic on the packaged release.
 # WM_NULL measures native message responsiveness, not Flutter frame rate.
 $child=[McxWindow]::FindWindowEx($script:handle,[IntPtr]::Zero,'FLUTTERVIEW',$null)
 if($child -eq [IntPtr]::Zero) { throw 'Flutter child window missing' }
 $times=@()
 for($i=0;$i -lt 40;$i++) {
  if($i % 10 -eq 0) { [void][McxWindow]::SetWindowPos($script:handle,[IntPtr]::Zero,0,0,(1000+($i%20)*10),800,6) }
  $delta=if($i -lt 20){-120}else{120}
  $rect=New-Object McxWindow+Rect
  [void][McxWindow]::GetWindowRect($child,[ref]$rect)
  $x=[int](($rect.left+$rect.right)/2)
  $y=[int](($rect.top+$rect.bottom)/2)
  [void][McxWindow]::PostMessage($child,0x20A,[IntPtr]($delta*65536),[IntPtr](($y -shl 16) -bor ($x -band 65535)))
  $watch=[Diagnostics.Stopwatch]::StartNew()
  $reply=[IntPtr]::Zero
  if([McxWindow]::SendMessageTimeout($child,0,[IntPtr]::Zero,[IntPtr]::Zero,2,2000,[ref]$reply) -eq [IntPtr]::Zero) { throw 'Flutter child window stopped processing native messages' }
  $times += $watch.Elapsed.TotalMilliseconds
  Start-Sleep -Milliseconds 25
 }
 $sorted=@($times | Sort-Object)
 @{nativeMessageP95Ms=$sorted[[int]($sorted.Count*.95)-1];nativeMessageMaxMs=$sorted[-1];samples=$times.Count;note='Native message responsiveness, not Flutter frame times'} | ConvertTo-Json | Tee-Object diagnostics/interaction.json
 if(Test-Path "$env:MCX_SMOKE_REPORT.log") { Copy-Item "$env:MCX_SMOKE_REPORT.log" diagnostics/startup.log -Force }
 [void][McxWindow]::PostMessage($script:handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 if(!$process.WaitForExit(8000)) { throw 'Window close stalled' }
 if($process.ExitCode -ne 0) { throw "Application exited with code $($process.ExitCode)" }
 if(Test-Path "$env:MCX_SMOKE_REPORT.frames.json") { Copy-Item "$env:MCX_SMOKE_REPORT.frames.json" diagnostics/frames.json;Get-Content diagnostics/frames.json }
 else { throw 'Release frame metrics were not written' }
 'Editor initialized, wheel/resize exercised, and native main window closed successfully.'
} catch {
 if(Test-Path "$env:MCX_SMOKE_REPORT.log") { Copy-Item "$env:MCX_SMOKE_REPORT.log" diagnostics/startup.log -Force; Get-Content diagnostics/startup.log }
 Get-Content diagnostics/stderr.txt -ErrorAction SilentlyContinue
 try {
  Add-Type -AssemblyName System.Windows.Forms
  Add-Type -AssemblyName System.Drawing
  $bounds=[Windows.Forms.SystemInformation]::VirtualScreen
  $bitmap=New-Object Drawing.Bitmap $bounds.Width,$bounds.Height
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  try { $graphics.CopyFromScreen($bounds.Location,[Drawing.Point]::Empty,$bounds.Size);$bitmap.Save("$PWD/diagnostics/desktop.png") }
  finally { $graphics.Dispose();$bitmap.Dispose() }
 } catch { Write-Warning "Screenshot unavailable: $_" }
 throw
} finally {
 if($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
 Remove-Item Env:MCX_SMOKE_REPORT -ErrorAction SilentlyContinue
 Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}
