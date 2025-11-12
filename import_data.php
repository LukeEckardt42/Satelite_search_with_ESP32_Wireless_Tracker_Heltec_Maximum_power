<?php
// --- Konfiguration ---
$servername = "localhost";
$username = "root"; // Dein DB-Benutzer
$password = "";     // Dein DB-Passwort
$dbname = "satellitendatenbank"; // Deine DB
$csv_file = "satellite_log_db_format.csv"; // CSV-Datei vom C-Skript

// --- Hilfsfunktionen zur NMEA-Konvertierung ---

/**
 * Wandelt NMEA-Koordinaten (DDMM.mmmmN/S/E/W) in Dezimalgrad um.
 * z.B. "4807.038N" -> 48.1173
 * z.B. "01131.000E" -> 11.5166
 */
function parseNMEACoord($coord) {
    $is_neg = (strpos($coord, 'S') !== false || strpos($coord, 'W') !== false);
    $coord = preg_replace("/[NSEW]/", "", $coord); // Buchstaben entfernen
    $data = (float)$coord;

    if ($data == 0) return 0;

    // Prüfen, ob Längen- (DDDMM.m) oder Breitengrad (DDMM.m)
    if (strlen(explode('.', $coord)[0]) > 4) { 
        // Längengrad (DDDMM.m)
        $deg = floor($data / 100);
        $min = $data - ($deg * 100);
    } else {
        // Breitengrad (DDMM.m)
        $deg = floor($data / 100);
        $min = $data - ($deg * 100);
    }
    
    $decimal = $deg + ($min / 60);
    
    return $is_neg ? -$decimal : $decimal;
}

/**
 * Wandelt NMEA-Datum (DDMMYY) und Zeit (HHMMSS.ss) 
 * in ein SQL-DateTime-Format (YYYY-MM-DD HH:MM:SS) um.
 */
function parseNMEADateTime($nmea_date, $nmea_time) {
    if (strlen($nmea_date) != 6 || strlen($nmea_time) < 6) {
        return null;
    }
    
    // Datum (DDMMYY)
    $day = substr($nmea_date, 0, 2);
    $month = substr($nmea_date, 2, 2);
    $year = "20" . substr($nmea_date, 4, 2); // Annahme 21. Jhd.
    
    // Zeit (HHMMSS.ss)
    $hour = substr($nmea_time, 0, 2);
    $minute = substr($nmea_time, 2, 2);
    $second = substr($nmea_time, 4, 2);
    
    return "$year-$month-$day $hour:$minute:$second";
}

// -------------------------------------------------
// --- Haupt-Import-Logik ---
// -------------------------------------------------

// 1. Datenbankverbindung herstellen
$conn = new mysqli($servername, $username, $password, $dbname);
if ($conn->connect_error) {
    die("Datenbankverbindung fehlgeschlagen: " . $conn->connect_error . "\n");
}
echo "Datenbankverbindung erfolgreich.\n";

// 2. CSV-Datei öffnen
if (($handle = fopen($csv_file, "r")) !== FALSE) {
    
    // Header-Zeile überspringen
    fgetcsv($handle, 1000, ","); 

    // 3. SQL-Statement vorbereiten (MIT der neuen 'system'-Spalte)
    $sql = "INSERT INTO satellitendaten (datetime, latitude, longitude, system, sat_id, elevation, azimuth, snr) 
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    
    $stmt = $conn->prepare($sql);
    
    // 8 Parameter binden: s (datetime), d (lat), d (lon), s (system), i (id), i (elev), i (azim), i (snr)
    // HINWEIS: SNR wird als integer (i) behandelt, NULL-Werte werden unten gesetzt.
    $stmt->bind_param("sddsiiii", 
        $datetime_db, 
        $lat_db, 
        $lon_db, 
        $system_db, 
        $sat_id_db, 
        $elev_db, 
        $azim_db, 
        $snr_db
    );

    echo "Starte Import von '$csv_file'...\n";

    // 4. Jede Zeile der CSV verarbeiten
    while (($data = fgetcsv($handle, 1000, ",")) !== FALSE) {
        
        // Prüfen, ob wir die erwarteten 8 Spalten haben
        if (count($data) == 8) {
            
            // Spalte 0: NMEA Datum/Zeit (z.B. "230394 123519.00")
            $datetime_parts = explode(' ', $data[0]);
            if (count($datetime_parts) == 2) {
                $datetime_db = parseNMEADateTime($datetime_parts[0], $datetime_parts[1]);
            } else {
                $datetime_db = null; // Ungültiges Format
            }

            // Spalte 1 & 2: Lat/Lon (NMEA-Format umwandeln)
            $lat_db = parseNMEACoord($data[1]);
            $lon_db = parseNMEACoord($data[2]);

            // Spalte 3: System (NEU)
            $system_db = $data[3]; // z.B. "GPS (USA)"

            // Spalte 4-7: Satellitendaten (Indizes sind +1 verschoben)
            $sat_id_db = (int)$data[4];
            $elev_db   = (int)$data[5];
            $azim_db   = (int)$data[6];
            
            // SNR (kann "N/A" sein)
            $snr_val = $data[7];
            $snr_db = ($snr_val == "N/A" || $snr_val == "") ? 0 : (int)$snr_val; // 0 statt NULL für int-Bindung

            // Führe das Statement nur aus, wenn Datum und Position gültig sind
            if ($datetime_db !== null && $lat_db != 0) {
                if (!$stmt->execute()) {
                    echo "Fehler beim Einfügen: " . $stmt->error . "\n";
                }
            }

        } else {
            echo "Überspringe Zeile: Falsche Spaltenanzahl (" . count($data) . ")\n";
        }
    }
    
    // 5. Aufräumen
    $stmt->close();
    fclose($handle);
    echo "Import abgeschlossen.\n";

} else {
    echo "Fehler: Konnte '$csv_file' nicht öffnen.\n";
}

$conn->close();
?>