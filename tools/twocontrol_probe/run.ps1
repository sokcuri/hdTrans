param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = 'Stop'
$probeStarted = Get-Date
$Root = [IO.Path]::GetFullPath($Root)
$harness = Join-Path $Root 'authored-harness'
$homeDir = Join-Path $Root 'disposable-home'
New-Item -ItemType Directory $harness, $homeDir | Out-Null
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
$exe = Join-Path $harness 'Probe.exe'
& $csc /nologo /platform:x86 /target:exe "/out:$exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll (Join-Path $PSScriptRoot 'Probe.cs')
if ($LASTEXITCODE -ne 0) { throw 'Harness compilation failed' }

function New-CleanStart([string]$Mode, [string]$Extra = '') {
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.Arguments = "$Mode `"$Root`" $Extra"
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $harness
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $start.Environment.Clear()
    foreach ($name in @('SystemRoot','WINDIR','PATH','COMSPEC','ProgramFiles','ProgramFiles(x86)','ProgramW6432','NUMBER_OF_PROCESSORS','PROCESSOR_ARCHITECTURE')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $start.Environment[$name] = $value }
    }
    foreach ($name in @('HOME','USERPROFILE','APPDATA','LOCALAPPDATA','TEMP','TMP')) { $start.Environment[$name] = $homeDir }
    $start.Environment['HDTRANS_CACHE_DIR'] = 'off'
    return $start
}

foreach ($mode in @('gui','plugin')) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = New-CleanStart $mode
    [void]$process.Start()
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(45000)) {
        $inspect = [Diagnostics.Process]::new()
        $inspect.StartInfo = New-CleanStart 'inspect' "$($process.Id)"
        [void]$inspect.Start()
        $inspectOut = $inspect.StandardOutput.ReadToEndAsync()
        $inspectErr = $inspect.StandardError.ReadToEndAsync()
        if (!$inspect.WaitForExit(15000)) { $inspect.Kill($true) }
        $inspectOut.Result | Set-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-blocked.log")
        $process.Kill($true)
        $process.WaitForExit()
        "TIMEOUT $mode normal initialization or call did not complete" | Set-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-status.log")
    } else {
        "EXIT $mode $($process.ExitCode)" | Set-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-status.log")
    }
    $output.Result | Set-Content -Encoding utf8 (Join-Path $Root "evidence/$mode.log")
    $errors.Result | Set-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-stderr.log")
    Get-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-status.log")
    Get-Content -Encoding utf8 (Join-Path $Root "evidence/$mode.log")
    if (Test-Path (Join-Path $Root "evidence/$mode-blocked.log")) { Get-Content -Encoding utf8 (Join-Path $Root "evidence/$mode-blocked.log") }
}
Start-Sleep -Seconds 3
$faults = @()
$events = Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000; StartTime=$probeStarted} -ErrorAction SilentlyContinue
foreach ($event in $events) {
    [xml]$xml = $event.ToXml()
    $fields = @{}
    foreach ($entry in $xml.Event.EventData.Data) { $fields[$entry.Name] = $entry.'#text' }
    if ($fields.AppName -notin @('TwoControl.exe','Probe.exe')) { continue }
    $faults += @{ AppName=$fields.AppName; ModuleName=$fields.ModuleName; ExceptionCode=$fields.ExceptionCode; FaultingOffset=$fields.FaultingOffset }
}
ConvertTo-Json -InputObject @($faults) -Depth 3 | Set-Content -Encoding utf8 (Join-Path $Root 'evidence/native-crash-events.json')
Get-Content -Encoding utf8 (Join-Path $Root 'evidence/native-crash-events.json')
