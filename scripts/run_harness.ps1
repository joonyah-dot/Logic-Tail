param([switch]$Fresh)

# Logic-Tail vst3_harness test runner
# Parameter index map (verified via dump-params on 2026-02-28):
#   0  Gravity       1  Size          2  Pre-Delay     3  Feedback (reverb)
#   4  Mod Depth     5  Mod Rate      6  Lo EQ         7  Hi EQ
#   8  Resonance     9  Freeze       10  Kill Dry      11  Time
#  12  Tempo Sync   13  Division     14  Feedback (delay)  15  Ping Pong
#  16  Mod Rate (delay)  17  Mod Depth (delay)  18  HP Filter  19  LP Filter
#  20  Routing      21  Balance      22  Mix           23  Input
#  24  Output       25  Bypass
# Duplicate display names "Feedback", "Mod Rate", "Mod Depth" are disambiguated by index
# in test case JSON files (paramsByIndex). Run with -Fresh to re-check after plugin changes.

$ErrorActionPreference = "Stop"
$root    = Split-Path $PSScriptRoot -Parent
$build   = Join-Path $root "build"
$scripts = Join-Path $root "scripts"
$tests   = Join-Path $root "tests"

# -- Build -----------------------------------------------------------------------
if ($Fresh -or -not (Test-Path $build)) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    cmake -S $root -B $build -DBUILD_VST3_HARNESS=ON | Out-Null
}
Write-Host "Building LogicTail_VST3 and vst3_harness..." -ForegroundColor Yellow
cmake --build $build --config Debug --target LogicTail_VST3 vst3_harness

# -- Locate artefacts ------------------------------------------------------------
$harnessExe = Join-Path $build "tools\vst3_harness\vst3_harness_artefacts\Debug\vst3_harness.exe"
if (-not (Test-Path $harnessExe)) {
    Write-Error "vst3_harness.exe not found at $harnessExe"
}

$pluginBin = Join-Path $build `
    "LogicTail_artefacts\Debug\VST3\Logic-Tail.vst3\Contents\x86_64-win\Logic-Tail.vst3"
if (-not (Test-Path $pluginBin)) {
    Write-Error "Plugin binary not found at $pluginBin"
}

Write-Host "Harness: $harnessExe" -ForegroundColor DarkGray
Write-Host "Plugin:  $pluginBin"  -ForegroundColor DarkGray

# -- Artifacts directory ----------------------------------------------------------
$ts        = Get-Date -Format "yyyyMMdd_HHmmss"
$artifacts = Join-Path $root "artifacts\$ts"
New-Item -ItemType Directory -Force $artifacts | Out-Null

# -- Generate test WAVs -----------------------------------------------------------
$wavDir = Join-Path $artifacts "wavs"
New-Item -ItemType Directory -Force $wavDir | Out-Null
Write-Host "`nGenerating test WAVs..." -ForegroundColor Yellow
python "$scripts\gen_test_wavs.py" --outdir $wavDir --sr 44100 --seconds 3 --channels 2

$impulse = Join-Path $wavDir "impulse.wav"
if (-not (Test-Path $impulse)) { Write-Error "gen_test_wavs.py failed to create impulse.wav" }

# -- dump-params ------------------------------------------------------------------
Write-Host "`n=== dump-params ===" -ForegroundColor Cyan
& $harnessExe dump-params --plugin $pluginBin | Tee-Object (Join-Path $artifacts "params.txt")

# -- Helper: render a case + analyze ---------------------------------------------
$passCount = 0
$failCount = 0

function Invoke-Case {  # approved verb: Invoke
    param([string]$Name, [string]$CaseJson)
    Write-Host "`n=== render: $Name ===" -ForegroundColor Cyan
    $outDir = Join-Path $artifacts $Name
    New-Item -ItemType Directory -Force $outDir | Out-Null

    & $harnessExe render `
        --plugin $pluginBin `
        --in    $impulse `
        --outdir $outDir `
        --sr 44100 --bs 512 --ch 2 `
        --case $CaseJson

    if ($LASTEXITCODE -ne 0) {
        Write-Warning "render FAILED for $Name (exit $LASTEXITCODE)"
        $script:failCount++
        return
    }

    $wetWav = (Get-ChildItem $outDir -Filter "*.wav" -ErrorAction SilentlyContinue |
               Select-Object -First 1).FullName
    if (-not $wetWav) {
        Write-Warning "No output WAV found for $Name"
        $script:failCount++
        return
    }

    Write-Host "=== analyze: $Name ===" -ForegroundColor Cyan
    & $harnessExe analyze `
        --dry  $impulse `
        --wet  $wetWav `
        --outdir $outDir `
        --auto-align --null

    if ($LASTEXITCODE -eq 2) {
        Write-Warning "NaN/Inf DETECTED in $Name  - plugin instability!"
        $script:failCount++
    } else {
        $script:passCount++
    }
}

# -- Helper: verify ping-pong L/R asymmetry from per-channel metrics --------------
# True ping-pong alternates echoes strictly L then R, so L and R RMS must differ
# significantly. If they're within 1 dB of each other, the effect isn't working.
function Test-PingPong {
    param([string]$Name)
    $metricsFile = Join-Path $artifacts "$Name\metrics.json"
    if (-not (Test-Path $metricsFile)) { return }

    $m = Get-Content $metricsFile | ConvertFrom-Json
    $perCh = $m.wetRmsDbfsPerChannel
    if ($perCh.Count -lt 2) {
        Write-Warning "  [ping-pong check] Not enough channels in $Name metrics"
        $script:failCount++
        return
    }

    $lRms = [double]$perCh[0]
    $rRms = [double]$perCh[1]
    $diff = [Math]::Abs($lRms - $rRms)

    if ($diff -lt 1.0) {
        Write-Warning "  [ping-pong check] FAIL: L RMS $([Math]::Round($lRms,1)) dB, R RMS $([Math]::Round($rRms,1)) dB -- diff $([Math]::Round($diff,1)) dB (expected > 1 dB for alternating echoes)"
        $script:failCount++
        $script:passCount--  # undo the pass counted by Invoke-Case
    } else {
        Write-Host "  [ping-pong check] PASS: L RMS $([Math]::Round($lRms,1)) dB, R RMS $([Math]::Round($rRms,1)) dB -- diff $([Math]::Round($diff,1)) dB" -ForegroundColor Green
    }
}

# -- Run all cases ----------------------------------------------------------------
$cases = Join-Path $tests "cases"
if (-not (Test-Path $cases)) {
    Write-Error "No tests\cases directory found at $cases"
}

Get-ChildItem $cases -Filter "*.json" | Sort-Object Name | ForEach-Object {
    Invoke-Case -Name $_.BaseName -CaseJson $_.FullName
    # Extra spatial check for ping-pong cases
    if ($_.BaseName -like "*pingpong*") {
        Test-PingPong -Name $_.BaseName
    }
}

# Summary
Write-Host "`n======================================" -ForegroundColor White
Write-Host "  PASSED: $passCount   FAILED: $failCount" -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Red" })
Write-Host "  Artifacts: $artifacts" -ForegroundColor DarkGray
Write-Host "======================================" -ForegroundColor White

if ($failCount -gt 0) { exit 1 }
