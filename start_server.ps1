#  start_server.ps1  —  מגדיר משתני סביבה ומריץ את השרת
#  הרץ מתוך תיקיית build שבה נמצא IrisRecognitionServer.exe:
#      cd server\build2\Release..\..\..\start_server.ps1
param(
    [string]$ExePath = "IrisRecognitionServer.exe"
)

#  הגדרת מפתחות AES-256 (64 תווי hex = 32 בייט)
#  שנה את הערכים האלה למפתחות האמיתיים שלך!
$env:IRIS_AES_KEY    = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
$env:IRIS_DB_AES_KEY = "1f1e1d1c1b1a191817161514131211100f0e0d0c0b0a09080706050403020100"
#  נתיבי קבצי TLS — ברירת מחדל: server.crt/server.key בתיקיית העבודה
#  שנה אם הקבצים נמצאים במקום אחר
$env:IRIS_TLS_CERT = "server.crt"
$env:IRIS_TLS_KEY  = "server.key"
#  הרצת השרת
if (-not (Test-Path $ExePath)) {
    Write-Error "לא נמצא קובץ הרצה: $ExePath"
    Write-Host "בצע build תחילה ואז הרץ שוב את הסקריפט."
    exit 1
}
Write-Host "מריץ שרת: $ExePath" -ForegroundColor Cyan
Write-Host "  IRIS_AES_KEY    = $($env:IRIS_AES_KEY.Substring(0,8))..." -ForegroundColor DarkGray
Write-Host "  IRIS_DB_AES_KEY = $($env:IRIS_DB_AES_KEY.Substring(0,8))..." -ForegroundColor DarkGray
Write-Host "  TLS CERT        = $env:IRIS_TLS_CERT" -ForegroundColor DarkGray
Write-Host "  TLS KEY         = $env:IRIS_TLS_KEY" -ForegroundColor DarkGray
Write-Host ""
& $ExePath
