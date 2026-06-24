# Wipes build output and all generated asset caches for a clean rebuild
$build = "$PSScriptRoot\build"
Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue
Get-ChildItem -Path "$PSScriptRoot\assets" -Recurse -Include "*.texcache","*.objcache" | Remove-Item -Force
Write-Host "Cache Cleaned" -ForegroundColor Green
Write-Host "./run to build with clean state." -ForegroundColor Green