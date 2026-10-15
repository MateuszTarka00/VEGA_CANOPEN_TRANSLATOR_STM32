param(
    [string]$Compiler = "gcc"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("canopen-tests-" + [guid]::NewGuid() + ".exe")
$modules = @("canopen", "canopen_sdo", "canopen_nmt", "canopen_heartbeat",
             "canopen_emcy", "object_dictionary", "persistent_storage",
             "factory_identity", "bootloader_handoff")
$sources = @((Join-Path $PSScriptRoot "test_canopen.c"))
foreach ($module in $modules) {
    $sources += Join-Path $root ("Core\Src\" + $module + ".c")
}
try {
    & $Compiler "-std=c99" "-Wall" "-Werror" ("-I" + (Join-Path $root "Core\Inc")) @sources "-o" $output
    if ($LASTEXITCODE -ne 0) { throw "Native compilation failed: $LASTEXITCODE" }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "CANopen tests failed: $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
}
