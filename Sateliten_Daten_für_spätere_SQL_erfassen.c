#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // string.h enthält strtok

// --- Globale Variablen für den "Zustand" (Position & Zeit) ---
char g_last_date[16] = "N/A";
char g_last_time[16] = "N/A";
char g_last_lat[16] = "N/A";
char g_last_lon[16] = "N/A";

// --- Globale Variablen für die 5-Sekunden-Drossel ---
const DWORD LOG_INTERVAL_MS = 5000; 
static DWORD g_last_log_time = 0;

// --- Globale Variablen für die Zeilenverarbeitung ---
char line_buffer[1024]; 
int line_pos = 0;       

// Hilfsfunktion: Wandelt Talker ID in Systemnamen um
const char* get_system_name(char t1, char t2) {
    if (t1 == 'G' && t2 == 'P') return "GPS (USA)";
    if (t1 == 'G' && t2 == 'L') return "GLONASS (Russisch)";
    if (t1 == 'G' && t2 == 'A') return "Galileo (Europaeisch)";
    if (t1 == 'G' && t2 == 'B') return "BeiDou (Chinesisch)";
    return "Unknown";
}

// Hilfsfunktion: Kopiert ein Feld (Token) sicher
void safe_strncpy(char *dest, const char *src, size_t n, int max_len) {
    // Cast zu (int) oder (size_t) bei max_len, um Compiler-Warnung zu beheben
    if ((int)n >= max_len) n = max_len - 1; 
    strncpy(dest, src, n);
    dest[n] = '\0';
}

// ----------------------------------------------------
// KORRIGIERT: Parser für GPRMC (Position, Datum, Zeit)
// Verwendet jetzt Standard strtok
// ----------------------------------------------------
void process_rmc(char *rmc_line) {
    char *token;
    int field_count = 0;
    
    char *p = rmc_line + 7; // Wir brauchen nur den Teil nach dem "$GPRMC,"
    char status = 'V';      // (V=Void, A=Active)

    while ((token = strtok(p, ","))) {
        if (p) p = NULL; // Wichtig: Beim nächsten Aufruf NULL übergeben
        
        field_count++;
        switch (field_count) {
            case 1: // Zeit (HHMMSS.ss)
                safe_strncpy(g_last_time, token, 9, sizeof(g_last_time));
                break;
            case 2: // Status
                status = token[0];
                break;
            case 3: // Latitude (DDMM.mmmm)
                if (status == 'A') safe_strncpy(g_last_lat, token, 15, sizeof(g_last_lat));
                break;
            case 4: // N/S
                if (status == 'A') strncat(g_last_lat, token, 1); // Fügt N oder S hinzu
                break;
            case 5: // Longitude (DDDMM.mmmm)
                if (status == 'A') safe_strncpy(g_last_lon, token, 15, sizeof(g_last_lon));
                break;
            case 6: // E/W
                if (status == 'A') strncat(g_last_lon, token, 1); // Fügt E oder W hinzu
                break;
            case 9: // Datum (DDMMYY)
                if (status == 'A') safe_strncpy(g_last_date, token, 15, sizeof(g_last_date));
                break;
        }
    }
}

// ----------------------------------------------------
// KORRIGIERT: Parser für GSV (Satelliten), jetzt mit Drossel
// Verwendet Standard strtok und behebt Logikfehler
// ----------------------------------------------------
void process_gsv(char *gsv_line, FILE *output_file) {
    
    // --- 1. Drosselung prüfen ---
    DWORD current_time = GetTickCount();
    if (current_time - g_last_log_time < LOG_INTERVAL_MS) {
        return; // Noch keine 5 Sekunden um
    }
    
    // --- LOGIK-FIX: Temporäre Kopie NUR für den Check ---
    char line_copy_for_check[1024];
    strcpy(line_copy_for_check, gsv_line);

    char *p_check = line_copy_for_check + 7;
    char *token_check;

    token_check = strtok(p_check, ","); // Feld 1 (Sätze gesamt)
    if (p_check) p_check = NULL;
    token_check = strtok(NULL, ",");    // Feld 2 (Dieser Satz Nr.)
    
    if (token_check && strcmp(token_check, "1") == 0) {
        // Dies ist der erste Satz des Blocks. Zeitstempel jetzt setzen.
        g_last_log_time = current_time; 
    } else if (g_last_log_time == 0) {
         // Falls wir mittenrein starten, setze Timer beim ersten Log
         g_last_log_time = current_time;
    }

    // --- 2. System bestimmen (vom Original gsv_line) ---
    const char* system_name = get_system_name(gsv_line[1], gsv_line[2]);

    // --- 3. Satelliten parsen (vom Original gsv_line) ---
    // gsv_line wurde durch den Check oben NICHT verändert
    char *p = gsv_line;
    char *token; 
    int field_count = 0;
    char sat_data[4][32];
    int sat_field_index = 0;

    while ((token = strtok(p, ","))) { 
        if (p) p = NULL; 

        field_count++;
        if (field_count <= 4) continue; // Überspringe Header ($GPGSV, 3, 1, 11)

        safe_strncpy(sat_data[sat_field_index], token, strlen(token), 32);
        sat_field_index++;

        if (sat_field_index == 4) {
            if (strlen(sat_data[0]) > 0) {
                char* id     = (strlen(sat_data[0]) == 0) ? "N/A" : sat_data[0];
                char* elev   = (strlen(sat_data[1]) == 0) ? "N/A" : sat_data[1];
                char* azimut = (strlen(sat_data[2]) == 0) ? "N/A" : sat_data[2];
                char* snr    = (strlen(sat_data[3]) == 0) ? "N/A" : sat_data[3];
                
                // Checksumme vom SNR entfernen (falls vorhanden)
                char *checksum = strchr(snr, '*');
                if (checksum) *checksum = '\0';

                // --- 4. Kombinierte Daten in CSV schreiben ---
                fprintf(output_file, "%s %s,%s,%s,%s,%s,%s,%s,%s\n",
                        g_last_date, g_last_time,
                        g_last_lat, g_last_lon,
                        system_name, id, elev, azimut, snr);
                        
                printf("%s %s,%s,%s,%s,%s,%s,%s,%s\n",
                        g_last_date, g_last_time,
                        g_last_lat, g_last_lon,
                        system_name, id, elev, azimut, snr);
            }
            sat_field_index = 0;
        }
    }
    fflush(output_file);
}


// ----------------------------------------------------
// Haupt-Verarbeitungsfunktion (entscheidet, welcher Parser genutzt wird)
// ----------------------------------------------------
void process_line(FILE *output_file) {
    line_buffer[line_pos] = '\0'; 

    // Wir brauchen eine Kopie, da strtok den String verändert
    char line_copy[1024];
    strcpy(line_copy, line_buffer);

    // 1. Filtern: GSV (Satelliten) oder RMC (Position/Zeit)?
    if (line_pos > 7 && line_buffer[0] == '$' && 
        line_buffer[3] == 'G' && line_buffer[4] == 'S' && line_buffer[5] == 'V') {
        
        process_gsv(line_copy, output_file);
    
    } else if (line_pos > 7 && line_buffer[0] == '$' &&
               line_buffer[3] == 'R' && line_buffer[4] == 'M' && line_buffer[5] == 'C') {
        
        process_rmc(line_copy);
    }
    
    line_pos = 0; // Puffer für die nächste Zeile zurücksetzen
}


// ----------------------------------------------------
// MAIN: (Fast unverändert)
// ----------------------------------------------------
int main() {
    FILE *output_file = fopen("satellite_log_db_format.csv", "w");
    if (output_file == NULL) {
        perror("Fehler beim Öffnen der Datei 'satellite_log_db_format.csv'");
        return 1;
    }
    
    // NEUER CSV-Header (passend zur DB)
    fprintf(output_file, "datetime,latitude,longitude,system,sat_id,elevation,azimut,snr_db\n");
    fflush(output_file);

    HANDLE hSerial;
    // HIER DEINEN COM-PORT ANPASSEN (z.B. "\\\\.\\COM11")
    hSerial = CreateFile(
        "\\\\.\\COM9", GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0
    );
    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Fehler beim Öffnen von COM11. Fehlercode: %lu\n", GetLastError());
        fclose(output_file);
        return 1;
    }

    // 3. Port konfigurieren
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
         fprintf(stderr, "Fehler: GetCommState. Fehlercode: %lu\n", GetLastError());
         CloseHandle(hSerial);
         fclose(output_file);
         return 1;
    }

    // WICHTIG: Baudrate muss zu heltec.ino passen! (115200)
    dcbSerialParams.BaudRate = CBR_115200; 
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        fprintf(stderr, "Fehler: SetCommState. Fehlercode: %lu\n", GetLastError());
        CloseHandle(hSerial);
        fclose(output_file);
        return 1;
    }

    // 4. Timeouts setzen
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50; 
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hSerial, &timeouts)) {
        fprintf(stderr, "Fehler: SetCommTimeouts. Fehlercode: %lu\n", GetLastError());
        CloseHandle(hSerial);
        fclose(output_file);
        return 1;
    }

    // 5. Endlosschleife: Daten lesen und verarbeiten
    printf("Starte GSV/RMC-Logger (5 Sek. Intervall) auf COM4...\n");
    printf("Schreibe in 'satellite_log_db_format.csv'\n");
    printf("datetime,latitude,longitude,system,sat_id,elevation,azimut,snr_db\n");
    printf("---------------------------------------------------\n");

    char read_buffer[256];
    DWORD bytes_read = 0; 

    while (1) {
        if (!ReadFile(hSerial, read_buffer, sizeof(read_buffer), &bytes_read, NULL)) {
            fprintf(stderr, "Fehler beim Lesen vom Port. Fehlercode: %lu\n", GetLastError());
            break; 
        }
        if (bytes_read > 0) {
            for (DWORD i = 0; i < bytes_read; i++) {
                char c = read_buffer[i];
                if (c == '\n') {
                    process_line(output_file);
                } else if (c != '\r') {
                    // Cast zu size_t, um Compiler-Warnung zu beheben
                    if ((size_t)line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = c;
                    } else {
                        line_pos = 0;
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