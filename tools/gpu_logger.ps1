# Logs GPU load, clocks, power and throttle reasons once a second to a CSV while a game runs (nvidia-smi).
# Stops itself after -Minutes (default 45) so it can never be forgotten. Read-only: it does not touch the game or the GPU state.
#   powershell -File gpu_logger.ps1 -Out gpu_log.csv [-Minutes 45]
param([string]$Out = "gpu_log.csv", [int]$Minutes = 45)
$q = 'timestamp,utilization.gpu,utilization.memory,clocks.current.sm,clocks.current.memory,power.draw,temperature.gpu,memory.used,clocks_event_reasons.active'
$end = (Get-Date).AddMinutes($Minutes)
& nvidia-smi --query-gpu=$q --format=csv -l 1 2>&1 | ForEach-Object {
    if ((Get-Date) -gt $end) { break }
    $_ | Add-Content $Out
}
