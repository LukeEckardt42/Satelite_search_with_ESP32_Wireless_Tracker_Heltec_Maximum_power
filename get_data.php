<?php
// V2.4: Behebt das "Wackel-Problem" (Mikro-Glitches) beim Senden.

// PHP-Header (Fehler ausblenden, JSON-Typ)
ini_set('display_errors', 0);
ini_set('memory_limit', '1024M'); // Mehr Speicher für 1M+ Sätze
set_time_limit(600); // 10 Minuten max.
error_reporting(0);
header('Content-Type: application/json');

// ----- 1. Datenbank-Konfiguration (DEINE SERVER-DATEN) -----
// (Du musst diese eventuell auf deine lokalen XAMPP-Werte (root, "") ändern)
$servername = ""; // z.B. db123456789.hosting-data.io
$username = "";
$password = "";
$dbname = "";

// ----- 2. Verbindung herstellen -----
$conn = new mysqli($servername, $username, $password, $dbname);
if ($conn->connect_error) {
    die(json_encode(['error' => 'DB-Verbindung fehlgeschlagen: ' . $conn->connect_error]));
}
$conn->set_charset("utf8mb4");

// ----- 3. Zeit-Parameter abfragen -----
$range = $_GET['range'] ?? '1h'; // Standard 1h
$sql_filter = "";

switch ($range) {
    case '1h':
        $sql_filter = "WHERE timestamp >= NOW() - INTERVAL 1 HOUR";
        break;
    case '12h':
        $sql_filter = "WHERE timestamp >= NOW() - INTERVAL 12 HOUR";
        break;
    case '24h':
        $sql_filter = "WHERE timestamp >= NOW() - INTERVAL 1 DAY";
        break;
    case '7d':
        $sql_filter = "WHERE timestamp >= NOW() - INTERVAL 7 DAY";
        break;
    case 'all':
    default:
        $sql_filter = ""; // Alle Daten
        break;
}

// ----- 4. SQL-Abfrage (HOLT ALLE DATEN) -----
// (Optimiert dank V2.5 DB-Schema)
$sql = "
    SELECT 
        timestamp, sat_id, elevation, azimut, snr, lat, lon 
    FROM 
        satellite_tracks
    $sql_filter
    ORDER BY 
        timestamp ASC"; // WICHTIG: Nach Zeit sortieren!

$result = $conn->query($sql);

if (!$result) {
    die(json_encode(['error' => 'SQL-Fehler: ' . $conn->error]));
}

// ----- 5. DATEN-GRUPPIERUNG (V2.4 FIX) -----

$data_frames = [];
$current_frame = null;
$current_time_str = null;
$fallback_lat = 50.65; // Saalfeld
$fallback_lon = 11.36;

if ($result->num_rows > 0) {
    while($row = $result->fetch_assoc()) {
        
        $timestamp_key = $row['timestamp'];
        // Lese Lat/Lon (oder nutze Fallback, falls RMC fehlte)
        $lat = ($row['lat'] != 0) ? (float)$row['lat'] : $fallback_lat;
        $lon = ($row['lon'] != 0) ? (float)$row['lon'] : $fallback_lon;
        
        $satellite = [
            'id' => $row['sat_id'],
            'elev' => (int)$row['elevation'],
            'azim' => (int)$row['azimut'],
            'snr' => (int)$row['snr']
        ];

        if ($timestamp_key !== $current_time_str) {
            // Speichere den *vorherigen* Frame (nachdem er final gefiltert wurde)
            if ($current_frame !== null) {
                // Konvertiere das Satelliten-Map zurück in ein Array
                $current_frame['satellites'] = array_values($current_frame['satellites']);
                $data_frames[] = $current_frame;
            }
            
            // Starte einen *neuen* Frame
            $current_time_str = $timestamp_key;
            $current_frame = [
                'datetime' => $timestamp_key,
                'lat' => $lat,
                'lon' => $lon,
                'satellites' => [] // WICHTIG: Als leeres Array (Map) initialisieren
            ];
            
            // Aktualisiere Fallbacks für den (seltenen) Fall, 
            // dass der nächste Frame *nur* GSV-Sätze hat.
            $fallback_lat = $lat;
            $fallback_lon = $lon;
        }
        
        // --- DER V2.4 "WACKEL-FIX" ---
        // Statt: $current_frame['satellites'][] = $satellite; (erzeugt Duplikate)
        // Nutzen wir die sat_id als "Key". Das überschreibt automatisch
        // frühere Einträge (z.B. G7/snr:33) mit dem letzten (G7/snr:0).
        $current_frame['satellites'][$satellite['id']] = $satellite;
        
    } // Ende While-Schleife

    // Den allerletzten Frame nicht vergessen!
    if ($current_frame !== null) {
        $current_frame['satellites'] = array_values($current_frame['satellites']);
        $data_frames[] = $current_frame;
    }
}

// ----- 6. Sauberes, GEFRAMTES JSON senden -----
$conn->close();
echo json_encode($data_frames);
?>