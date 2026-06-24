$make = "C:\msys64\mingw64\bin\mingw32-make.exe"
Set-Location "$PSScriptRoot\build"
& $make -j4
if ($LASTEXITCODE -eq 0) { Write-Host "OK" -ForegroundColor Green; .\byahengine.exe }
else { Write-Host "FAILED" -ForegroundColor Red }
Set-Location $PSScriptRoot