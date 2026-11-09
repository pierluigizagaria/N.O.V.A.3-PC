param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Routine)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$receipt = Get-Content (Join-Path $repo 'local/tools/ghidra-toolchain.json') -Raw | ConvertFrom-Json
$state = Join-Path $repo 'local/tools/ghidra-state'
# Invoke the installed launcher class directly: LaunchSupport -save writes to
# roaming AppData before honoring Ghidra's application settings directory.
& (Join-Path $receipt.java_home 'bin/java.exe') `
    '-Djava.system.class.loader=ghidra.GhidraClassLoader' `
    '-Djava.awt.headless=true' `
    "-Dapplication.settingsdir=$state/settings" `
    "-Dapplication.cachedir=$state/cache" `
    "-Dapplication.tempdir=$state/temp" `
    -cp (Join-Path $receipt.install_dir 'Ghidra/Framework/Utility/lib/Utility.jar') `
    ghidra.Ghidra ghidra.app.util.headless.AnalyzeHeadless `
    "$repo/local/analysis/ghidra" nova-original -process libNOVA3_neon.so `
    -noanalysis -scriptPath $PSScriptRoot -postScript DecompileGameplay.java `
    "$repo/local/analysis/ghidra/gameplay" @Routine
if ($LASTEXITCODE -ne 0) { throw 'Gameplay decompilation failed' }
