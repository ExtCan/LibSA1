<#
.SYNOPSIS
  Headless Ghidra import + auto-analysis + export for SADC raw SH-4 binaries.

.DESCRIPTION
  Imports a raw little-endian SH-4 image (BinaryLoader, SuperH4:LE:32:default) into
  the Ghidra project sadc/build/ghidra/projects/<Name>.gpr, applies the memory map and
  function seeds (scripts/SadcSetup.java, pre-analysis), runs auto-analysis, then
  scripts/SadcExport.java writes to sadc/build/ghidra/<Name>/:
     functions.json   list of {name, entry, end, size, ranges, block, tags, thunk,
                      noreturn, signature, calls, called_by, decomp, decomp_ms}
     xrefs.json       {entry: {reads, writes, pool}} global data refs per function
     summary.json     per-memory-block function counts/coverage, decomp stats, timing
     decomp/<ENTRY>.c Ghidra pseudo-C per function (ENTRY = 8 upper-case hex digits);
                      failures get a stub whose header says DECOMPILATION FAILED
     decomp_all.c     all of the above in one file (only with -DecompOut single|both)
     seeds_ptrscan.txt       pointer-scan seeds (gen_seeds.py), applied before analysis
     seeds_ptrscan_weak.txt  fallthrough-ambiguous pointer targets, applied after analysis
                             only where no code/function exists yet
     run.cmd          the exact command line that was executed (run_reuse.cmd for -Reuse);
                      it can be re-run directly: cmd /c "<OutDir>/run.cmd"
     run.json         binary, regions, seeds, start/end time, seconds, exit code
     logs/            headless.out (console), application.log, script.log
                      (-Reuse: headless_reuse.out, application_reuse.log, script_reuse.log)

  Tags in functions.json (how a function was found, besides Ghidra's own analysis):
    SADC_SEED      seed file / entry point (pre-analysis)
    SADC_WEAKSEED  weak pointer seed (post-analysis)
    SADC_ORPHAN    disassembled code that Ghidra left outside any function (e.g. JMP targets)
    SADC_GAPFILL   valid subroutine found in an undefined gap of an executable block
  SadcExport.java also removes functions that analysis/seeding created at SHC in-function
  long-branch targets (mov.l @(d,PC),Rn; jmp @Rn) and switch case targets (mova/braf
  tables), marks tail-call JMPs as CALL_RETURN, and gives FPSCR_PR/SZ/FR=0 to the decompiler
  (SadcSetup.java). See the header comments of both scripts for all options.

  Analysis option defaults: "Non-Returning Functions - Discovered" is disabled (it wrongly
  marked rand() and the FSCA sin/cos helpers non-returning and truncated their callers).

  To run another Ghidra script against the saved project (e.g. scripts/SadcDump.java to print
  a listing range), use ghidra_headless.ps1 (it bypasses analyzeHeadless.bat, which cannot
  handle the parentheses in this tree's path).

.EXAMPLE
  # full run on 1ST_READ.BIN (defaults: known memory map, entry 8C010000, pointer seeds)
  powershell -ExecutionPolicy Bypass -File sadc\tools\ghidra\run_ghidra.ps1

.EXAMPLE
  # re-export from the existing project without re-import / re-analysis
  powershell -ExecutionPolicy Bypass -File sadc\tools\ghidra\run_ghidra.ps1 -Reuse -NoGapFill

.EXAMPLE
  # an overlay (already PRS-decompressed) loaded at 0x8C500000, code+data in one rwx block
  powershell -ExecutionPolicy Bypass -File sadc\tools\ghidra\run_ghidra.ps1 `
      -Binary sadc\build\overlays\STG01.BIN -Name STG01 -Base 8C500000
#>
[CmdletBinding()]
param(
    [string]$Binary,
    [string]$Name,
    [string]$Base = "8C010000",
    # NAME:START-END:FLAGS[,...]  FLAGS: r w x v u(=new uninitialized block)
    [string]$Regions,
    [string]$Entry,
    [string[]]$Seeds = @(),
    [switch]$NoPtrSeeds,
    # "all", "none" or "S-E;S-E" (hex, by function entry)
    [string]$Decomp = "all",
    [int]$Timeout = 30,
    [int]$Threads = 0,
    [switch]$NoGapFill,
    [switch]$NoPools,
    [switch]$Reuse,
    [string]$OutDir,
    [string]$MaxMem = "16G",
    [int]$MaxCpu = 0,
    [int]$AnalysisTimeout = 0,
    # extra Ghidra analysis options "Name=Value" (applied after the defaults below), e.g.
    # "Decompiler Parameter ID=true" or "Non-Returning Functions - Discovered=true"
    [string[]]$AnalysisOptions = @(),
    # files = decomp/<ENTRY>.c, single = decomp_all.c only, both
    [ValidateSet("files", "single", "both")][string]$DecompOut = "files",
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"

$Sadc = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Ghidra = Join-Path $Sadc "tools\ghidra_12.1.4_PUBLIC"
$Jdk = Join-Path $Sadc "tools\jdk-21.0.12.1+1"
$ScriptDir = Join-Path $PSScriptRoot "scripts"
$Language = "SuperH4:LE:32:default"

if (-not $Binary) { $Binary = Join-Path $Sadc "disc\1ST_READ.BIN" }
$Binary = (Resolve-Path $Binary).Path
$FileName = Split-Path $Binary -Leaf
if (-not $Name) { $Name = [IO.Path]::GetFileNameWithoutExtension($Binary).ToUpper() }
if (-not $OutDir) { $OutDir = Join-Path $Sadc "build\ghidra\$Name" }
$ProjDir = Join-Path $Sadc "build\ghidra\projects"
$LogDir = Join-Path $OutDir "logs"
New-Item -ItemType Directory -Force $OutDir, $ProjDir, $LogDir | Out-Null

# build/ is generated output: keep it out of git
$gi = Join-Path $Sadc ".gitignore"
$giText = if (Test-Path $gi) { [IO.File]::ReadAllText($gi) } else { "" }
if ($giText -notmatch '(?m)^/?build/?\s*$') {
    [IO.File]::AppendAllText($gi, ($(if ($giText -and -not $giText.EndsWith("`n")) { "`n" } else { "" }) + "build/`n"))
}

$baseVal = [Convert]::ToUInt32($Base, 16)
$len = (Get-Item $Binary).Length
if (-not $Regions) {
    if ($FileName -ieq "1ST_READ.BIN" -and $baseVal -eq [Convert]::ToUInt32("8C010000", 16)) {
        # Section bounds from the startup section table at 0x8C111194 / 0x8C66C2C0 and
        # the first/last library functions (0x8C5FBB20 .. 0x8C66C2A4 + pool).
        # sysmem = 0x8C000000-0x8C00FFFF (IP.BIN / BIOS syscall vectors), uninitialized.
        # P4 / hardware register areas (0xA05F...., 0xFF......) are deliberately unmapped.
        $Regions = "sysmem:8C000000-8C010000:rwu," +
                   "text:8C010000-8C111194:rx,data:8C111194-8C5FBB20:rw," +
                   "lib_text:8C5FBB20-8C66C2C0:rx,lib_data:8C66C2C0-8C67C680:rw," +
                   "bss:8C67C680-8C900000:rwu"
        if (-not $Entry) { $Entry = "8C010000" }
    } else {
        $Regions = ("{0}:{1:X8}-{2:X8}:rwx" -f $Name, $baseVal, ($baseVal + $len))
    }
}

# code ranges (initialized + executable) for pointer seeding
$codeRanges = @()
foreach ($it in $Regions.Split(",")) {
    $p = $it.Trim().Split(":")
    if ($p.Length -ge 3 -and $p[2].Contains("x") -and -not $p[2].Contains("u")) { $codeRanges += $p[1] }
}

$seedFiles = @()
$weakSeeds = $null
foreach ($s in $Seeds) { if ($s) { $seedFiles += (Resolve-Path $s).Path } }
if (-not $NoPtrSeeds -and -not $Reuse -and $codeRanges.Count -gt 0) {
    $ptrSeeds = Join-Path $OutDir "seeds_ptrscan.txt"
    $weakSeeds = Join-Path $OutDir "seeds_ptrscan_weak.txt"
    $gsArgs = @((Join-Path $PSScriptRoot "gen_seeds.py"), $Binary, "--base", $Base, "-o", $ptrSeeds,
                "--weak-out", $weakSeeds)
    foreach ($r in $codeRanges) { $gsArgs += @("--code", $r) }
    Write-Host "gen_seeds: py -3.12 $($gsArgs -join ' ')"
    & py -3.12 @gsArgs
    if ($LASTEXITCODE -ne 0) { throw "gen_seeds.py failed" }
    $seedFiles += $ptrSeeds
}

# ---- assemble analyzeHeadless arguments (every one quoted in run.cmd) ----
$a = @($ProjDir, $Name)
if ($Reuse) {
    $a += @("-process", $FileName, "-noanalysis")
} else {
    $a += @("-import", $Binary, "-overwrite", "-loader", "BinaryLoader",
            "-loader-baseAddr", ("0x{0:X8}" -f $baseVal), "-loader-blockName", "image",
            "-processor", $Language, "-cspec", "default")
}
$sfx = $(if ($Reuse) { "_reuse" } else { "" })
$a += @("-scriptPath", $ScriptDir, "-log", (Join-Path $LogDir "application$sfx.log"),
        "-scriptlog", (Join-Path $LogDir "script$sfx.log"))
if ($MaxCpu -gt 0) { $a += @("-max-cpu", "$MaxCpu") }
if ($AnalysisTimeout -gt 0) { $a += @("-analysisTimeoutPerFile", "$AnalysisTimeout") }

$pre = @()
if (-not $Reuse) {
    $pre += "regions=$Regions"
    if ($Entry) { $pre += "entry=$Entry" }
}
if ($seedFiles.Count -gt 0) { $pre += ("seeds=" + ($seedFiles -join ";")) }
# Default: no non-returning-function discovery. On this binary it marked rand() (0x8C10D334),
# njSin/njCos-style FSCA helpers (0x8C639EC8/0x8C639F5C/0x8C63A1CC) etc. as noreturn,
# truncating hundreds of callers. Re-enable with -AnalysisOptions.
$defaultOptions = @("Non-Returning Functions - Discovered=false")
foreach ($o in ($defaultOptions + $AnalysisOptions)) { $pre += "opt.$o" }
if ($Reuse -and $seedFiles.Count -gt 0) { $pre += "analyze=1" }
if ($pre.Count -gt 0) { $a += @("-preScript", "SadcSetup.java") + $pre }

$post = @("out=$OutDir", "decomp=$Decomp", "timeout=$Timeout",
          ("gapfill=" + $(if ($NoGapFill) { "0" } else { "1" })),
          ("pools=" + $(if ($NoPools) { "0" } else { "1" })))
if ($Threads -gt 0) { $post += "threads=$Threads" }
$post += "decompout=$DecompOut"
if ($weakSeeds -and (Test-Path $weakSeeds)) { $post += "weakseeds=$weakSeeds" }
$a += @("-postScript", "SadcExport.java") + $post

$consoleLog = Join-Path $LogDir $(if ($Reuse) { "headless_reuse.out" } else { "headless.out" })
$cmdFile = Join-Path $OutDir $(if ($Reuse) { "run_reuse.cmd" } else { "run.cmd" })
$launcher = Join-Path $PSScriptRoot "ghidra_headless.ps1"
& $launcher -HeadlessArgs $a -Log $consoleLog -CmdFile $cmdFile -MaxMem $MaxMem -DryRun | Out-Null
Write-Host "command file: $cmdFile   (re-run: cmd /c `"$cmdFile`")"
Write-Host "console log : $consoleLog"
if ($DryRun) { Get-Content $cmdFile; return }

$t0 = Get-Date
& $cmdFile
$rc = $LASTEXITCODE
$t1 = Get-Date
$secs = [int]($t1 - $t0).TotalSeconds
$info = [ordered]@{ binary = $Binary; name = $Name; base = ("0x{0:X8}" -f $baseVal);
    regions = $Regions; entry = $Entry; seeds = $seedFiles; reuse = [bool]$Reuse;
    started = $t0.ToString("o"); finished = $t1.ToString("o"); seconds = $secs; exit_code = $rc }
$runJson = Join-Path $OutDir $(if ($Reuse) { "run_reuse.json" } else { "run.json" })
[IO.File]::WriteAllText($runJson, ($info | ConvertTo-Json -Depth 4))
Write-Host "analyzeHeadless exit code $rc after $secs s"
Get-Content $consoleLog -Tail 15
exit $rc
