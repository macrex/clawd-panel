$ErrorActionPreference = 'Continue'
# Tudo sai do ambiente: um caminho de usuario hardcoded aqui so vale na
# maquina que o escreveu.
$destino = Join-Path $env:USERPROFILE ".claude\metrics-api"
$out = Join-Path $destino "setup-resultado.txt"
$log = New-Object System.Collections.ArrayList
function Log($m) { [void]$log.Add($m) }

Log ("Elevado: " + ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))

# --- 1) Regra de firewall (entrada TCP 8787) ---
try {
    $existing = Get-NetFirewallRule -DisplayName "Claude Metrics API 8787" -ErrorAction SilentlyContinue
    if ($existing) {
        Log "FIREWALL: regra ja existia"
    } else {
        New-NetFirewallRule -DisplayName "Claude Metrics API 8787" `
            -Direction Inbound -Protocol TCP -LocalPort 8787 -Action Allow `
            -Profile Private,Domain -Description "Permite ESP32/LAN consumir metricas do Claude Code" | Out-Null
        Log "FIREWALL: regra criada (TCP 8787 entrada, perfis Private+Domain)"
    }
} catch {
    Log ("FIREWALL ERRO: " + $_.Exception.Message)
}

# --- 2) Tarefa agendada no logon ---
try {
    # pythonw, e nao python: a tarefa sobe sem janela de console.
    $py = (Get-Command pythonw.exe -ErrorAction Stop).Source
    $script = Join-Path $destino "claude_metrics_api.py"
    $action = New-ScheduledTaskAction -Execute $py -Argument "`"$script`""
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:COMPUTERNAME\$env:USERNAME"
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero)
    Register-ScheduledTask -TaskName "ClaudeMetricsAPI" -Action $action -Trigger $trigger -Settings $settings `
        -Description "API local de metricas do Claude Code (porta 8787)" -RunLevel Limited -User "$env:COMPUTERNAME\$env:USERNAME" -Force | Out-Null
    Log "TAREFA: ClaudeMetricsAPI registrada (dispara no logon)"
} catch {
    Log ("TAREFA ERRO: " + $_.Exception.Message)
}

# --- 3) Perfis de rede (diagnostico) ---
try {
    foreach ($p in (Get-NetConnectionProfile)) {
        Log ("REDE: " + $p.InterfaceAlias + " -> perfil " + $p.NetworkCategory)
    }
} catch {}

$log | Out-File -FilePath $out -Encoding UTF8
