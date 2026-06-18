# seed_data.ps1
# Inserts 5 sample users into IrisRecognitionDB via sp_EnrollUser
# Each IrisCode is 512 bytes: bits[256] (deterministic pattern) || mask[256] (all 0xFF)
# Run from PowerShell: .\database\seed_data.ps1

param(
    [string]$Server   = ".\SQLEXPRESS",
    [string]$Database = "IrisRecognitionDB"
)

$connStr = "Server=$Server;Database=$Database;Integrated Security=True;TrustServerCertificate=True"

function New-IrisCode([byte]$seed) {
    # bits[256]: each byte = seed XOR (index % 256)
    # mask[256]: all 0xFF  (every bit is valid)
    [byte[]]$code = New-Object byte[] 512
    for ($i = 0; $i -lt 256; $i++) {
        $code[$i]       = [byte]($seed -bxor ($i -band 0xFF))  # bits
        $code[$i + 256] = [byte]0xFF                           # mask
    }
    return ,$code   # comma forces return as single array, not unrolled
}

$users = @(
    @{ Passport="IL1234567"; Name="Sarah Cohen";  Nation="Israeli";   SeedL=0xA1; SeedR=0x5A },
    @{ Passport="US9876543"; Name="John Smith";   Nation="American";  SeedL=0x1F; SeedR=0xC0 },
    @{ Passport="JP5551234"; Name="Yuki Tanaka";  Nation="Japanese";  SeedL=0xDE; SeedR=0x0F },
    @{ Passport="ES1122334"; Name="Maria Garcia"; Nation="Spanish";   SeedL=0x7F; SeedR=0xF0 },
    @{ Passport="EG9988776"; Name="Ahmed Hassan"; Nation="Egyptian";  SeedL=0x12; SeedR=0xFE }
)

$conn = New-Object System.Data.SqlClient.SqlConnection $connStr
$conn.Open()
Write-Host "[DB] Connected to $Database on $Server" -ForegroundColor Cyan

# Clear existing data
$clear = $conn.CreateCommand()
$clear.CommandText = @"
DELETE FROM RecognitionLog;
DELETE FROM IrisFeatures;
DELETE FROM Users;
DBCC CHECKIDENT ('Users',          RESEED, 0);
DBCC CHECKIDENT ('IrisFeatures',   RESEED, 0);
DBCC CHECKIDENT ('RecognitionLog', RESEED, 0);
"@
$clear.ExecuteNonQuery() | Out-Null
Write-Host "[DB] Cleared existing data" -ForegroundColor Yellow

# Enroll users
$enrolledIDs = @()
foreach ($u in $users) {
    $cmd = $conn.CreateCommand()
    $cmd.CommandText = "sp_EnrollUser"
    $cmd.CommandType = [System.Data.CommandType]::StoredProcedure

    $cmd.Parameters.AddWithValue("@PassengerID", $u.Passport) | Out-Null
    $cmd.Parameters.AddWithValue("@FullName",        $u.Name)    | Out-Null
    $cmd.Parameters.AddWithValue("@Nationality",     $u.Nation)  | Out-Null

    # Explicitly type the binary params to avoid "No mapping" error.
    # Schema accepts up to 3 templates per eye (IrisLeft1..3 / IrisRight1..3);
    # the seed only populates template #1 and leaves 2/3 NULL.
    $pL = $cmd.Parameters.Add("@IrisLeft1",  [System.Data.SqlDbType]::VarBinary, 512)
    $pL.Value = [byte[]](New-IrisCode $u.SeedL)
    $pR = $cmd.Parameters.Add("@IrisRight1", [System.Data.SqlDbType]::VarBinary, 512)
    $pR.Value = [byte[]](New-IrisCode $u.SeedR)

    $outParam = $cmd.Parameters.Add("@NewUserID", [System.Data.SqlDbType]::Int)
    $outParam.Direction = [System.Data.ParameterDirection]::Output

    $cmd.ExecuteNonQuery() | Out-Null
    $newID = [int]$outParam.Value
    $enrolledIDs += $newID
    Write-Host ("  Enrolled: {0,-15} | {1,-15} | {2,-10} | ID={3}" -f $u.Passport, $u.Name, $u.Nation, $newID) -ForegroundColor Green
}

# Insert sample RecognitionLog entries
$logEntries = @(
    @{ UserIdx=0; Eye=0; OK=1; Dist=0.12; Note="Successful left-eye scan at gate A1"    },
    @{ UserIdx=0; Eye=1; OK=1; Dist=0.09; Note="Successful right-eye scan at gate A1"   },
    @{ UserIdx=1; Eye=0; OK=1; Dist=0.18; Note="Successful left-eye scan at gate B3"    },
    @{ UserIdx=1; Eye=0; OK=0; Dist=0.41; Note="Failed attempt - low quality image"     },
    @{ UserIdx=2; Eye=1; OK=1; Dist=0.15; Note="Successful right-eye scan at gate C2"   },
    @{ UserIdx=3; Eye=0; OK=1; Dist=0.07; Note="Successful left-eye scan at gate A1"    },
    @{ UserIdx=4; Eye=0; OK=0; Dist=0.38; Note="Failed attempt - possible spoofing"     },
    @{ UserIdx=4; Eye=0; OK=1; Dist=0.11; Note="Retry succeeded after repositioning"    },
    @{ UserIdx=2; Eye=1; OK=1; Dist=0.22; Note="Successful scan at border control"      }
)

# Fetch real IDs from DB in case output param was empty
$qid = $conn.CreateCommand()
$qid.CommandText = "SELECT UserID, PassengerID FROM Users ORDER BY UserID"
$ridR = $qid.ExecuteReader()
$idMap = @{}
while ($ridR.Read()) { $idMap[$ridR["PassengerID"]] = [int]$ridR["UserID"] }
$ridR.Close()
$passportOrder = $users | ForEach-Object { $_.Passport }
$enrolledIDs = $passportOrder | ForEach-Object { $idMap[$_] }
Write-Host ("[DB] Resolved User IDs: " + ($enrolledIDs -join ', ')) -ForegroundColor Yellow

foreach ($entry in $logEntries) {
    $uid = $enrolledIDs[$entry.UserIdx]
    $cmd = $conn.CreateCommand()
    $cmd.CommandText = "sp_LogAuthAttempt"
    $cmd.CommandType = [System.Data.CommandType]::StoredProcedure
    $cmd.Parameters.AddWithValue("@MatchedUserID",  $uid)          | Out-Null
    $cmd.Parameters.AddWithValue("@Eye",            $entry.Eye)    | Out-Null
    $cmd.Parameters.AddWithValue("@Success",        $entry.OK)     | Out-Null
    $cmd.Parameters.AddWithValue("@HammingDist",    $entry.Dist)   | Out-Null
    $cmd.Parameters.AddWithValue("@Notes",          $entry.Note)   | Out-Null
    $cmd.ExecuteNonQuery() | Out-Null
}
Write-Host ("[DB] Inserted {0} RecognitionLog entries" -f $logEntries.Count) -ForegroundColor Green

# Summary query
Write-Host "`n=== Users ===" -ForegroundColor Cyan
$q = $conn.CreateCommand()
$q.CommandText = @"
SELECT u.UserID, u.PassengerID, u.FullName, u.Nationality,
       COUNT(f.FeatureID) AS EyesRegistered
FROM Users u
LEFT JOIN IrisFeatures f ON f.UserID = u.UserID
GROUP BY u.UserID, u.PassengerID, u.FullName, u.Nationality
ORDER BY u.UserID
"@
$reader = $q.ExecuteReader()
while ($reader.Read()) {
    Write-Host ("  ID={0} | {1,-12} | {2,-15} | {3,-10} | Eyes={4}" -f
        $reader["UserID"], $reader["PassengerID"],
        $reader["FullName"], $reader["Nationality"], $reader["EyesRegistered"])
}
$reader.Close()

Write-Host "`n=== RecognitionLog (last 9) ===" -ForegroundColor Cyan
$q2 = $conn.CreateCommand()
$q2.CommandText = "SELECT TOP 9 LogID, MatchedUserID, Eye, Success, ROUND(HammingDistance,3) AS Dist, Notes FROM RecognitionLog ORDER BY LogID"
$r2 = $q2.ExecuteReader()
while ($r2.Read()) {
    $ok = if ($r2["Success"] -eq $true) { "MATCH   " } else { "NO_MATCH" }
    Write-Host ("  [{0}] User={1} Eye={2} {3} Dist={4}  {5}" -f
        $r2["LogID"], $r2["MatchedUserID"], $r2["Eye"],
        $ok, $r2["Dist"], $r2["Notes"])
}
$r2.Close()
$conn.Close()
Write-Host "`n[Done] Seed data inserted successfully." -ForegroundColor Cyan
