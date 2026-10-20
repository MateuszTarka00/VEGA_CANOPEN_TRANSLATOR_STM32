param(
    [string]$Compiler = "gcc"
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("canopen-tests-" + [guid]::NewGuid() + ".exe")
$lopOutput = Join-Path ([System.IO.Path]::GetTempPath()) ("lop-tests-" + [guid]::NewGuid() + ".exe")
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
    $lopIncludes = @("Core/Inc", "Drivers/STM32G0xx_HAL_Driver/Inc",
        "Drivers/CMSIS/Device/ST/STM32G0xx/Include", "Drivers/CMSIS/Include",
        "Middlewares/Third_Party/FreeRTOS/Source/include",
        "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2",
        "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM0") |
        ForEach-Object { "-I" + (Join-Path $root $_) }
    & $Compiler "-std=c99" "-Wall" "-Werror" "-Wno-int-to-pointer-cast" `
        "-DSTM32G0B1xx" "-DUSE_HAL_DRIVER" @lopIncludes `
        (Join-Path $PSScriptRoot "test_lop.c") `
        (Join-Path $root "Core/Src/canOpenLopDefinitions.c") "-o" $lopOutput
    if ($LASTEXITCODE -ne 0) { throw "LOP compilation failed: $LASTEXITCODE" }
    & $lopOutput
    if ($LASTEXITCODE -ne 0) { throw "LOP tests failed: $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
    if (Test-Path -LiteralPath $lopOutput) { Remove-Item -LiteralPath $lopOutput }
}
