#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <thread>
#include <stdlib.h>
#include <string>
#include <conio.h>
#include <process.h>				
#include <stdio.h>
#include <locale.h>
#include <tchar.h>
#include <strsafe.h>
#include <vector>
#define HAVE_STRUCT_TIMESPEC
#define CHECKERROR    1    // ativa função CheckForError
#include "CheckForError.h"
#define WHITE   FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE
#define HLRED   FOREGROUND_RED   | FOREGROUND_INTENSITY

using namespace std;

typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

HANDLE hAlarmMailslot; // Maislot para os alarmes
LPCTSTR alarmMailslotName = TEXT("\\\\.\\mailslot\\AlarmDisplay");
HANDLE hAlarmMailslotEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "hAlarmMailslotEvent");
string alarmType;

string messageFormater(char* buffer) {
    string finalMessage;
    string time, nseq, alarmID, type, prev;
    char* nextToken = NULL;
    vector <char*> splittedMessageValues;

    char* token = strtok_s(buffer, "|", &nextToken);
    while (token != NULL)
    {
        splittedMessageValues.push_back(token);
        token = strtok_s(NULL, "|", &nextToken);
    }

    time = splittedMessageValues[5];
    nseq = splittedMessageValues[0];
    alarmID = splittedMessageValues[2];
    alarmType = splittedMessageValues[1];
    prev = splittedMessageValues[4];

    finalMessage = time + " NSEQ:" + nseq + " ID ALARME:" + alarmID + " GRAU:" + alarmType + " PREV:" + prev;
    return finalMessage;
}


int main()
{   
    printf("Processo AlarmDisplay iniciando execucao\n");
    setlocale(LC_ALL, "Portuguese");//habilita a acentuação para o português

    HANDLE hEscEvent;
    HANDLE hCEvent;
    char buffer[256];
    DWORD numberOfBytesToRead;
    BOOL fileStatus;
    HANDLE hOut; // handle para a saída no console

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
        printf("Erro ao obter handle para a saída da console\n");
    
    hEscEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "EscEvent");
    CheckForError(hEscEvent);
    hCEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "CEvent");
    CheckForError(hCEvent);

    HANDLE Events[2] = { hEscEvent, hCEvent };

    hAlarmMailslot = CreateMailslot(
        alarmMailslotName,
        0,                             // sem tamanho máximo de mensagem
        MAILSLOT_WAIT_FOREVER,         // sem time-out
        (LPSECURITY_ATTRIBUTES)NULL); //  parametros de segurança padrão

    if (hAlarmMailslot == INVALID_HANDLE_VALUE)
    {
        printf("Erro na função CreateMailslot de alarmes! Código: %d\n", GetLastError());
        exit(0);
    }
    else {
        SetEvent(hAlarmMailslotEvent);
        printf("Mailslot de alarmes criado com sucesso.\n");
    }

    DWORD status;
    string message;
    DWORD CEventStatus = WAIT_OBJECT_0;
    int nTypeEvent;
    bool awake = TRUE; // status da thread: ativa ou inativa

    while (TRUE) {

        CEventStatus = WaitForSingleObject(hCEvent, 0);
        if (CEventStatus != WAIT_OBJECT_0 && awake) {
            awake = FALSE;
            SetConsoleTextAttribute(hOut, WHITE);
            printf("Thread AlarmDisplay bloqueando a pedido do usuario\n");
        }
        else if (CEventStatus == WAIT_OBJECT_0 && !awake) {
            awake = TRUE;
            SetConsoleTextAttribute(hOut, WHITE);
            printf("Thread AlarmDisplay acordando a pedido do usuario\n");
        }

        status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
        CheckForError(status);
        nTypeEvent = status - WAIT_OBJECT_0;

        if (nTypeEvent == 0) break;

        fileStatus = ReadFile(hAlarmMailslot, &buffer, sizeof(buffer), &numberOfBytesToRead, NULL);
        if (fileStatus == 0) {
            printf("Falha no ReadFile de alarmes, código: (%d)\n", GetLastError());
        } else {
            message = messageFormater(buffer);
            if(alarmType == "9"){
                SetConsoleTextAttribute(hOut, HLRED);
            }
            else {
                SetConsoleTextAttribute(hOut, WHITE);
            }
            cout << message << endl;
        }
    }

    CloseHandle(Events[0]);
    CloseHandle(Events[1]);
    CloseHandle(hAlarmMailslotEvent);
    CloseHandle(hAlarmMailslot);
    CloseHandle(hOut);
    printf( "Processo que exibe alarmes encerrando execucao\n");
    return EXIT_SUCCESS;
}
