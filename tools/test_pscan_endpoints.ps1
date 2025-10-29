# Quick smoke test for P-SCAN endpoints# Quick smoke test for P-SCAN endpoints

# Run helper externally first# Run helper externally first: cd C:\ef-map-overlay\build\src\helper\Debug; .\ef-overlay-helper.exe



$baseUrl = "http://127.0.0.1:38765"$baseUrl = "http://127.0.0.1:38765"

$authToken = "ef-overlay-dev-token-2025"$authToken = "ef-overlay-dev-token-2025"



Write-Host ""Write-Host ""

Write-Host "=== P-SCAN Endpoint Smoke Test ===" -ForegroundColor CyanWrite-Host "=== P-SCAN Endpoint Smoke Test ===" -ForegroundColor Cyan



# Test 1: GET (should return 404 - no scan data yet)# Test 1: GET (should return 404 - no scan data yet)

Write-Host ""Write-Host ""

Write-Host "[1/3] GET /api/pscan-data (expecting 404)..." -ForegroundColor YellowWrite-Host "[1/3] GET /api/pscan-data (expecting 404)..." -ForegroundColor Yellow

try {try {

    $response = Invoke-WebRequest -Uri "$baseUrl/api/pscan-data" -Method GET -Headers @{"X-EF-Helper-Auth" = $authToken} -UseBasicParsing    $response = Invoke-WebRequest -Uri "$baseUrl/api/pscan-data" `

    $statusCode = $response.StatusCode        -Method GET `

    Write-Host "  Status: $statusCode" -ForegroundColor Green        -Headers @{"X-EF-Helper-Auth" = $authToken} `

} catch {        -UseBasicParsing

    if ($_.Exception.Response.StatusCode -eq 404) {    $statusCode = $response.StatusCode

        Write-Host "  OK - 404 as expected (no data yet)" -ForegroundColor Green    Write-Host "  Status: $statusCode" -ForegroundColor Green

    } else {    Write-Host "  Body: $($response.Content)"

        $errMsg = $_.Exception.Message} catch {

        Write-Host "  ERROR: $errMsg" -ForegroundColor Red    if ($_.Exception.Response.StatusCode -eq 404) {

    }        Write-Host "  ✓ 404 as expected (no data yet)" -ForegroundColor Green

}    } else {

        $errMsg = $_.Exception.Message

# Test 2: POST sample scan data        Write-Host "  ✗ Unexpected error: $errMsg" -ForegroundColor Red

Write-Host ""    }

Write-Host "[2/3] POST /api/pscan-data (sample data)..." -ForegroundColor Yellow}

$sampleData = @{

    system_id = "30006368"# Test 2: POST sample scan data

    system_name = "US3-N2F"Write-Host ""

    scanned_at_ms = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()Write-Host "[2/3] POST /api/pscan-data (sample data)..." -ForegroundColor Yellow

    nodes = @($sampleData = @{

        @{    system_id = "30006368"

            id = "network-node-1"    system_name = "US3-N2F"

            name = "Network Node Alpha"    scanned_at_ms = [DateTimeOffset]::Now.ToUnixTimeMilliseconds()

            type = "NetworkNode"    nodes = @(

            owner_name = "TestPlayer"        @{

            distance_m = 125000.5            id = "network-node-1"

        },            name = "Network Node Alpha"

        @{            type = "NetworkNode"

            id = "network-node-2"            owner_name = "TestPlayer"

            name = "Network Node Beta"            distance_m = 125000.5

            type = "NetworkNode"        },

            owner_name = "AnotherPlayer"        @{

            distance_m = 89234.2            id = "network-node-2"

        }            name = "Network Node Beta"

    )            type = "NetworkNode"

} | ConvertTo-Json -Depth 3            owner_name = "AnotherPlayer"

            distance_m = 89234.2

try {        }

    $response = Invoke-RestMethod -Uri "$baseUrl/api/pscan-data" -Method POST -Headers @{"X-EF-Helper-Auth" = $authToken; "Content-Type" = "application/json"} -Body $sampleData -UseBasicParsing    )

    Write-Host "  OK - Status 200" -ForegroundColor Green} | ConvertTo-Json -Depth 3

    Write-Host "  Nodes received: $($response.nodes_received)" -ForegroundColor Cyan

} catch {try {

    $errMsg = $_.Exception.Message    $response = Invoke-RestMethod -Uri "$baseUrl/api/pscan-data" `

    Write-Host "  ERROR: $errMsg" -ForegroundColor Red        -Method POST `

}        -Headers @{"X-EF-Helper-Auth" = $authToken; "Content-Type" = "application/json"} `

        -Body $sampleData `

# Test 3: GET again (should return scan data)        -UseBasicParsing

Write-Host ""    Write-Host "  ✓ Status: 200 OK" -ForegroundColor Green

Write-Host "[3/3] GET /api/pscan-data (expecting scan data)..." -ForegroundColor Yellow    $respJson = $response | ConvertTo-Json

try {    Write-Host "  Response: $respJson"

    $response = Invoke-RestMethod -Uri "$baseUrl/api/pscan-data" -Method GET -Headers @{"X-EF-Helper-Auth" = $authToken} -UseBasicParsing} catch {

    Write-Host "  OK - Status 200" -ForegroundColor Green    $errMsg = $_.Exception.Message

    $sys = $response.system_name + " (" + $response.system_id + ")"    Write-Host "  ✗ POST failed: $errMsg" -ForegroundColor Red

    Write-Host "  System: $sys" -ForegroundColor Cyan}

    $nodeCount = $response.nodes.Count

    Write-Host "  Nodes found: $nodeCount" -ForegroundColor Cyan# Test 3: GET again (should return scan data)

    foreach ($node in $response.nodes) {Write-Host ""

        $distKm = [math]::Round($node.distance_m / 1000, 2)Write-Host "[3/3] GET /api/pscan-data (expecting scan data)..." -ForegroundColor Yellow

        $nodeLine = "    - " + $node.name + " (" + $node.owner_name + ") - " + $distKm + " km"try {

        Write-Host $nodeLine -ForegroundColor Gray    $response = Invoke-RestMethod -Uri "$baseUrl/api/pscan-data" `

    }        -Method GET `

} catch {        -Headers @{"X-EF-Helper-Auth" = $authToken} `

    $errMsg = $_.Exception.Message        -UseBasicParsing

    Write-Host "  ERROR: $errMsg" -ForegroundColor Red    Write-Host "  ✓ Status: 200 OK" -ForegroundColor Green

}    $sys = $response.system_name + " (" + $response.system_id + ")"

    Write-Host "  System: $sys" -ForegroundColor Cyan

Write-Host ""    $scanTime = Get-Date -UnixTimeMilliseconds $response.scanned_at_ms -Format 'HH:mm:ss'

Write-Host "=== Test Complete ===" -ForegroundColor Cyan    Write-Host "  Scanned: $scanTime" -ForegroundColor Cyan

    $nodeCount = $response.nodes.Count
    Write-Host "  Nodes found: $nodeCount" -ForegroundColor Cyan
    foreach ($node in $response.nodes) {
        $distKm = [math]::Round($node.distance_m / 1000, 2)
        $nodeLine = "    - " + $node.name + " (" + $node.owner_name + ") - " + $distKm + " km"
        Write-Host $nodeLine -ForegroundColor Gray
    }
} catch {
    $errMsg = $_.Exception.Message
    Write-Host "  ✗ GET failed: $errMsg" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== Test Complete ===" -ForegroundColor Cyan
