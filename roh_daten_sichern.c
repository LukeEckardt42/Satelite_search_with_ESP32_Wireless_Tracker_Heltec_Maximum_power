#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Globale Variablen für die Zeilenverarbeitung ---
char line_buffer[2048]; // Puffer für eine einzelne NMEA-Zeile
int line_pos = 0;       // Aktuelle Position im Zeilenpuffer

/**
 * @brief Verarbeitet eine vollständig empfangene Zeile.
 * Holt einen Zeitstempel und schreibt die Zeile in die CSV-Datei.
 */
void process_line(FILE *output_file) {
    line_buffer[line_pos] = '\0'; // String korrekt abschließen

    // Nur Zeilen verarbeiten, die Inhalt haben und mit '$' beginnen
    if (line_pos > 1 && line_buffer[0] == '$') {
        
        // 1. Aktuellen PC-Zeitstempel holen
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timestamp[64];
        sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        
        // 2. Zeitstempel und die Roh-Zeile in die CSV schreiben
        //    Wir setzen die NMEA-Zeile in Anführungszeichen, 
        //    falls sie selbst Kommas enthält.
        fprintf(output_file, "%s,\"%s\"\n", timestamp, line_buffer);
        
        // 3. Auch auf der Konsole ausgeben
        printf("%s,\"%s\"\n", timestamp, line_buffer);
        
        // Sofort in die Datei schreiben (wichtig für Live-Beobachtung)
        fflush(output_file);
    }

    line_pos = 0; // Puffer für die nächste Zeile zurücksetzen
}

// ----------------------------------------------------
// MAIN: Hauptprogramm
// ----------------------------------------------------
int main() {
    
    // 1. Ausgabedatei öffnen
    FILE *output_file = fopen("raw_nmea_log.csv", "w");
    if (output_file == NULL) {
        perror("Fehler beim Öffnen der Datei 'raw_nmea_log.csv'");
        return 1;
    }
    
    // CSV-Header (Spaltennamen) schreiben
    fprintf(output_file, "PC_Timestamp,NMEA_Sentence\n");
    fflush(output_file);

    // 2. Seriellen Port (COM9) öffnen
    HANDLE hSerial;
    hSerial = CreateFile(
        "\\\\.\\COM9", // <-- ANFRAGE: COM9
        GENERIC_READ, 
        0, 0, 
        OPEN_EXISTING, 
        FILE_ATTRIBUTE_NORMAL, 0
    );
    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Fehler beim Öffnen von COM9. Fehlercode: %lu\n", GetLastError());
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

    // WICHTIG: Baudrate muss zu heltec.ino passen!
    // Dein heltec.ino verwendet 115200 Baud
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
    printf("Starte RAW NMEA Logger auf COM9 (Baud: 115200)...\n");
    printf("Schreibe in 'raw_nmea_log.csv'\n(Strg+C zum Beenden)\n\n");
    printf("PC_Timestamp,NMEA_Sentence\n");
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
                    if ((size_t)line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = c;
                    } else {
                        // Pufferüberlauf (sehr lange Zeile), Zeile verwerfen
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