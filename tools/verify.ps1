# Runs the full local verification checklist and exits non-zero if anything
# fails. There is no hosted CI in this repository (a deliberate choice) - this
# script is the substitute: one command, one exit code, everything the README
# and docs/*.md checklists ask a contributor to do by hand.
#
#   pwsh tools/verify.ps1              # everything
#   pwsh tools/verify.ps1 -Quick       # skip dotnet, samples E2E and ASan
#   pwsh tools/verify.ps1 -Asan        # additionally build and test under ASan
#
# Locates the Visual Studio developer environment itself: in a plain shell -
# not a "Developer PowerShell for VS" - neither cl.exe nor ninja.exe is on
# PATH, and `cmake -B build` fails with "unable to find a build program
# corresponding to Ninja" before you even get to see a real error.
#
# Every step runs even if an earlier one failed, so a single pass tells you
# everything that is broken, not just the first thing. Exit code is 0 only if
# every step that ran succeeded.

param(
    [switch]$Quick,
    [switch]$Asan,
    [string]$BuildDir = "build",
    [string]$AsanBuildDir = "build-asan"
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$script:Results = @()

function Write-Step {
    param([string]$Name, [bool]$Ok, [double]$Seconds, [string]$Detail = "")
    $tag = if ($Ok) { "[ OK ]" } else { "[FAIL]" }
    $color = if ($Ok) { "Green" } else { "Red" }
    $line = "  {0} {1,-38} {2,6:N1}s" -f $tag, $Name, $Seconds
    if ($Detail) { $line += "   $Detail" }
    Write-Host $line -ForegroundColor $color
}

# Runs $Action, recording pass/fail/timing regardless of what happens inside -
# a thrown exception or a non-zero $LASTEXITCODE both count as failure, but
# neither stops the rest of the checklist from running.
function Invoke-Step {
    param([string]$Name, [scriptblock]$Action, [switch]$Skip, [string]$SkipReason = "")

    if ($Skip) {
        Write-Host ("  [SKIP] {0,-38}          {1}" -f $Name, $SkipReason) -ForegroundColor DarkGray
        $script:Results += [pscustomobject]@{ Name = $Name; Status = "SKIP"; Seconds = 0.0; Detail = $SkipReason }
        return
    }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ok = $true
    $detail = ""
    try {
        & $Action
    } catch {
        $ok = $false
        $detail = $_.Exception.Message
    }
    $sw.Stop()

    Write-Step -Name $Name -Ok $ok -Seconds $sw.Elapsed.TotalSeconds -Detail $detail
    $script:Results += [pscustomobject]@{
        Name    = $Name
        Status  = if ($ok) { "OK" } else { "FAIL" }
        Seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
        Detail  = $detail
    }
}

# Throws if the previous native command's exit code was non-zero. PowerShell
# does not do this on its own, so every external command below is followed by
# one of these - otherwise a failing `cmake --build` would be silently treated
# as a pass.
function Assert-LastExitCode {
    param([string]$Message)
    if ($LASTEXITCODE -ne 0) { throw "$Message (exit $LASTEXITCODE)" }
}

# A scriptblock, not a `function` - this is load-bearing. Calling
# Launch-VsDevShell.ps1 from inside a *named* PowerShell function causes the
# Microsoft.VisualStudio.DevShell module to recurse into the caller
# indefinitely (reproduced: the enclosing function re-runs from its own first
# line, over and over, without ever reaching the `& $devShell` line's return).
# Invoking the exact same code as a scriptblock (`& $EnterVsDevShell`, or
# inlined directly in a step below) does not have this problem. Do not
# "clean this up" into a function.
$EnterVsDevShell = {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }   # already in a dev shell

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022+ with the " +
              "'Desktop development with C++' workload, or run this from a Developer PowerShell."
    }

    $installPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installPath) {
        throw "vswhere found no Visual Studio installation with the C++ workload installed."
    }

    $devShell = Join-Path $installPath "Common7\Tools\Launch-VsDevShell.ps1"
    if (-not (Test-Path $devShell)) {
        throw "Launch-VsDevShell.ps1 not found under '$installPath'."
    }

    & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    Set-Location $RepoRoot   # Launch-VsDevShell resets the working directory

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "Entered the VS developer shell but cl.exe is still not on PATH."
    }
}

function Test-PortListening {
    param([int]$Port)
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue)
}

function Wait-Port {
    param([int]$Port, [int]$TimeoutSeconds = 30, [switch]$UntilClosed)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $listening = Test-PortListening -Port $Port
        if ($UntilClosed) { if (-not $listening) { return $true } }
        else { if ($listening) { return $true } }
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# Kills a process AND its children. A plain Stop-Process on a `dotnet run`
# PID leaves the actual app running (and its port bound) because `dotnet run`
# launches the built app as a child process - this is why the E2E steps below
# run the built .exe directly instead of `dotnet run`, but the /T is kept as a
# second line of defense in case that ever changes.
function Stop-ProcessTree {
    param([int]$ProcessId)
    if (-not $ProcessId) { return }
    try { & taskkill /F /T /PID $ProcessId 2>$null | Out-Null } catch {}
}

# --------------------------------------------------------------------------
# Step 1-3: configure, build, ctest (unit + interop + no-exceptions + fuzz
# corpus replay + examples - everything registered with add_test()).
# --------------------------------------------------------------------------

Invoke-Step "VS developer environment" { & $EnterVsDevShell }

Invoke-Step "configure + build" {
    cmake -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        -DMEMORYPACK_BUILD_TESTS=ON -DMEMORYPACK_BUILD_SAMPLES=ON -DMEMORYPACK_BUILD_EXAMPLES=ON
    Assert-LastExitCode "cmake configure failed"
    cmake --build $BuildDir
    Assert-LastExitCode "cmake build failed"
}

Invoke-Step "ctest (unit, interop, noexcept, fuzz replay, examples)" {
    ctest --test-dir $BuildDir --output-on-failure
    Assert-LastExitCode "ctest reported failures"
}

# --------------------------------------------------------------------------
# Step 4-7: cross-checked against the real C# MemoryPack / cs2cpp generator.
# Skipped under -Quick, so the script still gives a useful signal on a
# machine with no .NET SDK.
# --------------------------------------------------------------------------

Invoke-Step "fixtures vs C# MemoryPack (FormatProbe verify)" -Skip:$Quick -SkipReason "-Quick" {
    dotnet run --project tools/FormatProbe -c Release -- verify tests/fixtures
    Assert-LastExitCode "FormatProbe verify failed - a fixture no longer matches installed MemoryPack"
}

Invoke-Step "C# reads C++ bytes (FormatProbe check-cpp)" -Skip:$Quick -SkipReason "-Quick" {
    $cppFixtures = Join-Path $BuildDir "cpp-fixtures"
    if (Test-Path $cppFixtures) { Remove-Item -Recurse -Force $cppFixtures }
    $interopExe = Join-Path $BuildDir "tests\memorypack_interop_tests.exe"
    # The first argument is spelled out (rather than "") on purpose: Windows
    # PowerShell silently drops an empty-string argument when calling a native
    # executable, which shifts $cppFixtures into argv[1] (the fixture INPUT
    # dir) and leaves argv[2] absent, so the tool silently writes nothing.
    & $interopExe "tests/fixtures" $cppFixtures
    Assert-LastExitCode "memorypack_interop_tests (fixture dump) failed"
    dotnet run --project tools/FormatProbe -c Release -- check-cpp $cppFixtures
    Assert-LastExitCode "FormatProbe check-cpp failed - a C++-produced fixture did not round-trip through C#"
}

Invoke-Step "cs2cpp.Tests (generator snapshots)" -Skip:$Quick -SkipReason "-Quick" {
    dotnet test tools/cs2cpp.Tests -c Release
    Assert-LastExitCode "cs2cpp.Tests failed"
}

Invoke-Step "cs2cpp drift check (CppClient, ChatClient)" -Skip:$Quick -SkipReason "-Quick" {
    dotnet run --project tools/cs2cpp -c Release -- `
        samples/CSharpServer/Packets.cs -o samples/CppClient/packets.hpp --check
    Assert-LastExitCode "samples/CppClient/packets.hpp has drifted from samples/CSharpServer/Packets.cs"
    dotnet run --project tools/cs2cpp -c Release -- `
        samples/ChatServer/Packets.cs -o samples/ChatClient/packets.hpp --check
    Assert-LastExitCode "samples/ChatClient/packets.hpp has drifted from samples/ChatServer/Packets.cs"
}

# --------------------------------------------------------------------------
# Step 8: sample E2E, both directions. Each pair is already self-verifying
# (the client asserts every value and exits non-zero on a mismatch); this
# just automates starting the server, waiting for it to listen, running the
# client, and tearing the server down again.
# --------------------------------------------------------------------------

function Find-DotnetExe {
    param([string]$Project, [string]$Name)
    $exe = Get-ChildItem -Path (Join-Path $Project "bin/Release") -Recurse -Filter "$Name.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $exe) { throw "built $Name.exe not found under $Project/bin/Release - did dotnet build succeed?" }
    return $exe
}

# Starts $ServerExe, waits for it to listen on $Port, runs $ClientExe (which
# must exit non-zero on a mismatch - both sample clients already do), then
# tears the server down. Both exes must be run directly rather than via
# `dotnet run` / `dotnet <dll>`, which launch the real app as a CHILD process:
# killing the wrapper's PID would leave the child (and the bound port) alive.
function Invoke-SampleE2E {
    param([string]$ServerExe, [string]$ClientExe, [int]$Port)

    $proc = Start-Process -FilePath $ServerExe -PassThru -WindowStyle Hidden
    try {
        if (-not (Wait-Port -Port $Port -TimeoutSeconds 30)) {
            throw "$(Split-Path -Leaf $ServerExe) never started listening on port $Port within 30s"
        }
        & $ClientExe
        Assert-LastExitCode "$(Split-Path -Leaf $ClientExe) reported a mismatch"
    } finally {
        Stop-ProcessTree -ProcessId $proc.Id
        # Best-effort: give the OS a moment to release the listening socket so
        # a re-run right after this one does not fail to bind. TIME_WAIT on
        # already-established client<->server sockets can still linger
        # regardless - that is normal TCP, not a bug in this script.
        Wait-Port -Port $Port -TimeoutSeconds 10 -UntilClosed | Out-Null
    }
}

Invoke-Step "sample E2E: CSharpServer + CppClient" -Skip:$Quick -SkipReason "-Quick" {
    $cppClientExe = Join-Path $BuildDir "samples\CppClient.exe"
    if (-not (Test-Path $cppClientExe)) { throw "$cppClientExe not built - run with samples enabled" }

    dotnet build samples/CSharpServer -c Release | Out-Null
    Assert-LastExitCode "failed to build samples/CSharpServer"
    $serverExe = Find-DotnetExe -Project "samples/CSharpServer" -Name "CSharpServer"

    Invoke-SampleE2E -ServerExe $serverExe -ClientExe $cppClientExe -Port 25001
}

Invoke-Step "sample E2E: CppServer + CsClient" -Skip:$Quick -SkipReason "-Quick" {
    $cppServerExe = Join-Path $BuildDir "samples\CppServer.exe"
    if (-not (Test-Path $cppServerExe)) { throw "$cppServerExe not built - run with samples enabled" }

    dotnet build samples/CsClient -c Release | Out-Null
    Assert-LastExitCode "failed to build samples/CsClient"
    $clientExe = Find-DotnetExe -Project "samples/CsClient" -Name "CsClient"

    Invoke-SampleE2E -ServerExe $cppServerExe -ClientExe $clientExe -Port 25003
}

# --------------------------------------------------------------------------
# Step 9: MSVC AddressSanitizer, only with -Asan (it needs its own build tree
# and takes noticeably longer). UBSan is not available on MSVC - see
# docs/security.md#fuzzing for the WSL/libFuzzer path that covers it.
# --------------------------------------------------------------------------

Invoke-Step "ASan build + ctest" -Skip:(-not $Asan) -SkipReason "pass -Asan to include" {
    cmake -B $AsanBuildDir -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
        -DMEMORYPACK_BUILD_TESTS=ON -DMEMORYPACK_SANITIZE=address
    Assert-LastExitCode "ASan cmake configure failed"
    cmake --build $AsanBuildDir
    Assert-LastExitCode "ASan build failed"
    ctest --test-dir $AsanBuildDir --output-on-failure
    Assert-LastExitCode "ASan ctest reported failures"
}

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------

Write-Host ""
$failed = @($script:Results | Where-Object { $_.Status -eq "FAIL" })
$ran = @($script:Results | Where-Object { $_.Status -ne "SKIP" })

if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) of $($ran.Count) steps failed. See above." -ForegroundColor Red
    exit 1
}

Write-Host "All $($ran.Count) steps passed." -ForegroundColor Green
exit 0
