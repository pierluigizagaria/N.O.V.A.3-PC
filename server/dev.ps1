param(
    [ValidateSet('test','build','format','vet')][string]$Action = 'test',
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$receipt = Join-Path $repoRoot 'local/logs/sv03/toolchain.json'
$goTool = if (Test-Path -LiteralPath $receipt) {
    (Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json).go
} else {
    (Get-Command go -ErrorAction Stop).Source
}
$cacheRoot = Join-Path $repoRoot 'local/tools/go-cache'
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$env:GOCACHE = Join-Path $cacheRoot 'build'
$env:GOMODCACHE = Join-Path $cacheRoot 'modules'
$env:GOPATH = Join-Path $cacheRoot 'path'
$env:GOTOOLCHAIN = 'local'
$env:CGO_ENABLED = '0'
if ($Restore) {
    # Use only after development-download access has been explicitly authorized.
    $env:GOPROXY = 'https://proxy.golang.org'
    $env:GOSUMDB = 'sum.golang.org'
    & $goTool -C $PSScriptRoot mod tidy
    if ($LASTEXITCODE -ne 0) { throw 'Module restore failed' }
}
$env:GOPROXY = 'off'
$env:GOSUMDB = 'off'
switch ($Action) {
    'format' { & $goTool -C $PSScriptRoot fmt ./... }
    'test' { & $goTool -C $PSScriptRoot test -count=1 ./... }
    'vet' { & $goTool -C $PSScriptRoot vet ./... }
    'build' {
        $output = Join-Path $repoRoot 'local/server/bin/nova-server.exe'
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
        & $goTool -C $PSScriptRoot build -trimpath -o $output ./cmd/nova-server
    }
}
if ($LASTEXITCODE -ne 0) { throw "Go $Action failed" }
