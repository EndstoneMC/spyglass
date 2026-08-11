<#
.SYNOPSIS
    Launches the Bedrock client if needed, injects spyglass.dll, and tails the log.

.DESCRIPTION
    Must run elevated: opening a handle to a packaged process needs it.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\run.ps1
    powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -Preview
#>
[CmdletBinding()]
param(
    [switch]$Preview,
    [string]$BuildDir = "$PSScriptRoot\..\build\conan_out\build\RelWithDebInfo"
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "run this from an elevated prompt; injecting into a packaged app needs it"
}

if ($Preview) {
    $appId = 'Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe!Game'
    $package = 'Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe'
} else {
    $appId = 'Microsoft.MinecraftUWP_8wekyb3d8bbwe!Game'
    $package = 'MICROSOFT.MINECRAFTUWP_8wekyb3d8bbwe'
}

$injector = Join-Path $BuildDir 'spyglass-inject.exe'
if (-not (Test-Path $injector)) {
    Write-Error "no injector at $injector -- build first"
}

if (-not (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue)) {
    Write-Host "starting the client..."
    Start-Process "explorer.exe" -ArgumentList "shell:AppsFolder\$appId"
    $deadline = [datetime]::UtcNow.AddSeconds(90)
    while (-not (Get-Process -Name 'Minecraft.Windows' -ErrorAction SilentlyContinue)) {
        if ([datetime]::UtcNow -gt $deadline) { Write-Error "the client did not start within 90s" }
        Start-Sleep -Milliseconds 500
    }
    Write-Host "waiting for it to finish loading..."
    Start-Sleep -Seconds 12
}

$log = Join-Path $env:LOCALAPPDATA "Packages\$package\LocalCache\Local\spyglass\spyglass.log"
$before = if (Test-Path $log) { (Get-Item $log).Length } else { 0 }

& $injector
if ($LASTEXITCODE -ne 0) {
    Write-Error "injection failed with exit code $LASTEXITCODE"
}

Write-Host "`nlog: $log"
Write-Host "press INSERT in game to open the overlay; Ctrl+C to stop tailing`n"
$deadline = [datetime]::UtcNow.AddSeconds(15)
while (-not (Test-Path $log) -and [datetime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
if (-not (Test-Path $log)) {
    Write-Error "the payload loaded but wrote no log; check that $log is writable from the AppContainer"
}

Get-Content $log -Encoding UTF8 -Tail 200 -Wait
