# Grava as 4 imagens usando esptool 5.x com --before usb-reset (sem botoes).
param([string]$Port)

# esptool nao e um .exe instalado -- e o script bundled do PlatformIO, chamado
# pelo python do proprio venv dele (penv). Um caminho absoluto hardcoded aqui,
# como ja houve, nunca existe em lugar nenhum alem da maquina que o escreveu:
# tudo sai de $env:USERPROFILE, que e onde o PlatformIO se instala.
$pio     = Join-Path $env:USERPROFILE '.platformio'
$python  = Join-Path $pio 'penv\Scripts\python.exe'
$esptool = Join-Path $pio 'packages\tool-esptoolpy\esptool.py'
$build   = Join-Path $PSScriptRoot '..\.pio\build\board'
$bootapp = Join-Path $env:USERPROFILE '.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin'

foreach ($f in @($python, $esptool, "$build\bootloader.bin", "$build\partitions.bin", $bootapp, "$build\firmware.bin")) {
    if (-not (Test-Path $f)) { Write-Host "FALTANDO: $f"; exit 1 }
}

if (-not $Port) {
    # A porta fixa (COM6) rotava a cada reset. Acha pelo VID/PID da placa
    # (USB-Serial/JTAG nativo, 303A/1001), contando so a entidade cujo NOME
    # traz "(COMx)" -- esta placa enumera TRES dispositivos com esse mesmo
    # VID/PID (composite, JTAG/serial debug, porta serial), e contar todos
    # faria uma placa so parecer tres.
    $matches = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.DeviceID -match 'VID_303A.*PID_1001' -and $_.Name -match '\(COM\d+\)' } |
        ForEach-Object { [regex]::Match($_.Name, '\(COM(\d+)\)').Groups[1].Value } |
        Sort-Object -Unique
    if ($matches.Count -eq 0) { Write-Host 'SEM PORTA: nenhuma serial com VID 303A/PID 1001'; exit 1 }
    if ($matches.Count -gt 1) { Write-Host "MULTIPLAS PORTAS: COM$($matches -join ', COM') -- informe -Port"; exit 1 }
    $Port = "COM$($matches[0])"
}

# --after watchdog-reset, NAO hard-reset: em chips com USB-Serial/JTAG o modo
# download fica latchado e um hard reset comum reinicia de volta no bootloader,
# deixando a placa sem app rodando (tela apagada, serial muda).
& $python $esptool --chip esp32s3 -p $Port --before usb-reset --after watchdog-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size 16MB `
    0x0     "$build\bootloader.bin" `
    0x8000  "$build\partitions.bin" `
    0xe000  $bootapp `
    0x10000 "$build\firmware.bin"
