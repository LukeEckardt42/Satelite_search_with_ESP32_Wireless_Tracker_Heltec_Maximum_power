#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Für strchr(), strncpy()

// --- Globale Variablen für die Zeilenverarbeitung ---
char line_buffer[1024]; // Puffer für eine einzelne Zeile
int line_pos = 0;       // Aktuelle Position im Zeilenpuffer

// Hilfsfunktion: Wandelt die Talker ID (z.B. GP) in einen lesbaren Systemnamen um
const char* get_system_name(char talker1, char talker2) {
    if (talker1 == 'G' && talker2 == 'P') return "GPS (USA)";
    if (talker1 == 'G' && talker2 == 'L') return "GLONASS (Russisch)";
    if (talker1 == 'G' && talker2 == 'A') return "Galileo (Europaeisch)";
    if (talker1 == 'G' && talker2 == 'B') return "BeiDou (Chinesisch)";
    if (talker1 == 'G' && talker2 == 'Q') return "QZSS (Japanisch)";
    return "Unknown";
}

// ----------------------------------------------------
// NEUE, ROBUSTE PARSER-FUNKTION
// ----------------------------------------------------
void process_line(FILE *output_file) {
    line_buffer[line_pos] = '\0'; // String korrekt abschließen

    // 1. Filtern: Wir wollen NUR GSV-Sätze ($__GSV)
    if (line_pos < 7 || line_buffer[0] != '$' || 
        line_buffer[3] != 'G' || line_buffer[4] != 'S' || line_buffer[5] != 'V') {
        
        line_pos = 0; // Puffer zurücksetzen und Zeile ignorieren
        return;
    }

    // 2. Zeitstempel vom PC holen
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timestamp[64];
    sprintf(timestamp, "%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    
    // 3. System (Herkunft) bestimmen
    const char* system_name = get_system_name(line_buffer[1], line_buffer[2]);

    // 4. Checksumme (*) finden und String dort abschneiden
    //    Das verhindert, dass die Checksumme (z.B. *6E) als Datum geparst wird.
    char *checksum = strchr(line_buffer, '*');
    if (checksum) {
        *checksum = '\0'; // Setzt ein String-Ende-Zeichen an die Stelle des '*'
    }

    // 5. Manueller Parser (ersetzt strtok)
    char *p = line_buffer; // "p" ist unser Zeiger, der durch den String wandert
    char token[32];       // Kleiner Puffer für ein einzelnes Feld
    int field_count = 0;

    // Puffer für die 4 Datenfelder eines Satelliten
    char sat_data[4][32]; // [0]=ID, [1]=Elevation, [2]=Azimut, [3]=SNR
    int sat_field_index = 0; // Zählt von 0 bis 3

    while (*p) { // Solange der Zeiger nicht am Ende des Strings ('\0') ist
        char *delimiter = strchr(p, ','); // Finde das nächste Komma
        int len;

        if (delimiter) {
            // Komma gefunden
            len = delimiter - p; // Länge des Feldes = Position Komma - Position Zeiger
            p = delimiter + 1; // Setze Zeiger auf das Zeichen NACH dem Komma
        } else {
            // Kein Komma mehr gefunden, das ist das letzte Feld
            len = strlen(p);
            p += len; // Setze Zeiger auf das String-Ende
        }
        
        // Token (das Feld) sicher kopieren
        if (len >= 32) len = 31; // Verhindert Pufferüberlauf
        strncpy(token, p - (len + (delimiter ? 1 : 0)), len); // Kopiere das Feld
        token[len] = '\0';

        field_count++;

        // Die ersten 4 Felder überspringen (z.B. "$GPGSV", "3", "1", "11")
        if (field_count <= 4) {
            continue;
        }

        // --- Ab hier sind es die Satellitendaten ---

        // Speichere das Feld in unserem 4er-Puffer
        strcpy(sat_data[sat_field_index], token);
        sat_field_index++;

        // Wenn der 4er-Puffer voll ist (ID, El, Az, SNR), ...
        if (sat_field_index == 4) {
            // ... schreibe die Daten in die CSV.
            
            // Prüfe, ob eine Sat-ID vorhanden ist (wichtigstes Feld)
            if (strlen(sat_data[0]) > 0) {
                // Setze leere Felder auf "N/A" (Nicht Verfügbar)
                char* id     = (strlen(sat_data[0]) == 0) ? "N/A" : sat_data[0];
                char* elev   = (strlen(sat_data[1]) == 0) ? "N/A" : sat_data[1];
                char* azimut = (strlen(sat_data[2]) == 0) ? "N/A" : sat_data[2];
                char* snr    = (strlen(sat_data[3]) == 0) ? "N/A" : sat_data[3];

                // In CSV-Datei schreiben
                fprintf(output_file, "%s,%s,%s,%s,%s,%s\n",
                        timestamp, system_name, id, elev, azimut, snr);
                        
                // Auch auf Konsole ausgeben
                printf("%s,%s,%s,%s,%s,%s\n",
                        timestamp, system_name, id, elev, azimut, snr);
            }
            
            // Setze den Index für den 4er-Puffer zurück
            sat_field_index = 0;
        }
    } // Ende der while-Schleife (Zeile ist fertig geparst)

    fflush(output_file); // Sofort in die Datei schreiben
    line_pos = 0; // Puffer für die nächste Zeile zurücksetzen
}


// ----------------------------------------------------
// MAIN: Hauptprogramm (unverändert)
// ----------------------------------------------------
int main() {
    // 1. Ausgabedatei öffnen
    FILE *output_file = fopen("satellite_log.csv", "w");
    if (output_file == NULL) {
        perror("Fehler beim Öffnen der Datei 'satellite_log.csv'");
        return 1;
    }
    
    // CSV-Header (Spaltennamen)
    fprintf(output_file, "PC_Timestamp,System,Sat_ID,Elevation,Azimut,SNR_dB\n");
    fflush(output_file);

    // 2. Seriellen Port (COM4) öffnen
    HANDLE hSerial;
    hSerial = CreateFile(
        "\\\\.\\COM4", GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 //Stelle den Richtigen COM Port ein
    );
    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Fehler beim Öffnen von COM4. Fehlercode: %lu\n", GetLastError());
        fclose(output_file);
        return 1;
    }

    // 3. Port konfigurieren
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
         fprintf(stderr, "Fehler: GetCommState. Fehlercode: %lu\n", GetLastError());
         /* ... restliche Fehlerbehandlung ... */
         return 1;
    }

    // -----------------------------------------------------------------
    // !! WICHTIG !!
    // Passe die Baudrate an deinen Arduino Sketch an (z.B. Serial.begin(9600))
    dcbSerialParams.BaudRate = CBR_9600; 
    // -----------------------------------------------------------------
    
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Fehler: SetCommState. Fehlercode: %lu\n", GetLastError());
        /* ... restliche Fehlerbehandlung ... */
        return 1;
    }

    // 4. Timeouts setzen
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50; 
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hSerial, &timeouts)) {
        fprintf(stderr, "Fehler: SetCommTimeouts. Fehlercode: %lu\n", GetLastError());
        /* ... restliche Fehlerbehandlung ... */
        return 1;
    }

    // 5. Endlosschleife: Daten lesen und verarbeiten
    printf("Starte ROBUSTEN GSV-Satelliten-Logger auf COM4...\n");
    printf("Schreibe in 'satellite_log.csv'\n(Strg+C zum Beenden)\n\n");
    printf("PC_Timestamp,System,Sat_ID,Elevation,Azimut,SNR_dB\n");
    printf("---------------------------------------------------\n");

    char read_buffer[256]; // Puffer für gelesene "Brocken"
    DWORD bytes_read = 0; 

    while (1) {
        if (!ReadFile(hSerial, read_buffer, sizeof(read_buffer), &bytes_read, NULL)) {
            fprintf(stderr, "Fehler beim Lesen vom Port. Fehlercode: %lu\n", GetLastError());
            break;
        }

        if (bytes_read > 0) {
            // Verarbeite jedes gelesene Zeichen einzeln
            for (DWORD i = 0; i < bytes_read; i++) {
                char c = read_buffer[i];

                if (c == '\n') {
                    // Zeilenende gefunden! Verarbeite die Zeile.
                    process_line(output_file);
                } else if (c != '\r') { // Ignoriere Carriage Return ('\r')
                    // Füge das Zeichen zum Zeilenpuffer hinzu
                    if (line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = c;
                    } else {
                        line_pos = 0; // Pufferüberlauf, Zeile verwerfen
                    }
                }
            }
        }
    }

    // 6. Aufräumen
    CloseHandle(hSerial);
    fclose(output_file);
    return 0;
}
