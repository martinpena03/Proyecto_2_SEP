# =============================================================================
# uart_session.ps1 - Registra la UART de la Zybo (115200 8N1) y, opcionalmente, envía
# teclas a la consola de diagnóstico en instantes programados.
#
# Sep Hero (IEE2463, Proyecto 2). Basado en uart_log.ps1 de la AYU10.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File sw\scripts\uart_session.ps1 -Segundos 30 `
#       -Log uart.log -Enviar "8000:s;9000:a;10000:1"
#   -Enviar : lista "milisegundos:texto" separada por ';' (tiempo desde que abre el puerto).
#             En el texto, \r se envía como retorno de carro.
# =============================================================================
param([int]$Segundos = 20, [string]$Puerto = 'COM4', [string]$Log = "$PSScriptRoot\uart.log",
      [string]$Enviar = '')
$sp = New-Object System.IO.Ports.SerialPort $Puerto, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 500
$sp.Open()
$cola = @()
if ($Enviar) {
    foreach ($it in $Enviar.Split(';')) {
        $i = $it.IndexOf(':')
        if ($i -gt 0) { $cola += ,@([int]$it.Substring(0, $i), $it.Substring($i + 1).Replace('\r', "`r")) }
    }
}
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$out = New-Object System.Text.StringBuilder
$k = 0
while ($sw.ElapsedMilliseconds -lt ($Segundos * 1000)) {
    while ($k -lt $cola.Count -and $sw.ElapsedMilliseconds -ge $cola[$k][0]) {
        $sp.Write($cola[$k][1]); $k++
    }
    try { $txt = $sp.ReadExisting(); if ($txt) { [void]$out.Append($txt) } } catch {}
    Start-Sleep -Milliseconds 20
}
$sp.Close()
$out.ToString() | Set-Content -Path $Log -Encoding utf8
$out.ToString()
