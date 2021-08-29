#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <thread>
#include <stdlib.h>
#include <string>
#include <strsafe.h>
#include <conio.h>
#include <process.h>				
#include <stdio.h>
#include <locale.h>
#define HAVE_STRUCT_TIMESPEC
#define CHECKERROR    1    // ativa função CheckForError
#include <vector>
#include "CheckForError.h"
#define DISK_FILE_SIZE 200

using namespace std;

typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

string messageFormater(char* buffer) {

    string finalMessage;
    string nseq, time, tag, value, unit, mode;
    char* nextToken = NULL;
    vector <char*> splittedMessageValues;
    char* timeArray;
    int i = 0;

    char* token = strtok_s(buffer, "|", &nextToken);
    while (token != NULL)
    {
        splittedMessageValues.push_back(token);
        token = strtok_s(NULL, "|", &nextToken);
    }

    nseq = splittedMessageValues[0];
    time = splittedMessageValues[6];
    tag = splittedMessageValues[2];
    value = splittedMessageValues[3];
    unit = splittedMessageValues[4];
    mode = splittedMessageValues[5];

    timeArray = &time[0];
    string cleanTime = strtok_s(timeArray, "Ì", &nextToken);

    finalMessage = " NSEQ: " + nseq + " HORA: " + cleanTime + " TAG: " + tag + " VALOR: " + value + " UE: " + unit + " MODO: " + mode;
    return finalMessage;
}

int main()
{
    setlocale(LC_ALL, "Portuguese");//habilita a acentuação para o português

    HANDLE hEscEvent;
    HANDLE hOEvent;
    HANDLE hDataWrittenEvent;
    HANDLE hMutex;
    LONG filePointer;
    HANDLE hDataFile;
    BOOL fileStatus;
    string message;

    hEscEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "EscEvent");
    CheckForError(hEscEvent);
    hOEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "OEvent");
    CheckForError(hOEvent);
    hDataWrittenEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, "DataWrittenEvent");
    CheckForError(hDataWrittenEvent);
    hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, "hMutex");
    CheckForError(hMutex);

    HANDLE Events[2] = { hEscEvent, hOEvent };
    
    DWORD status;
    int nTypeEvent;
    bool awake = TRUE; // status da thread: ativa ou inativa
    int nMessages = 0;
    char buffer[52];
    DWORD numberOfBytesToRead;
    DWORD OEventStatus = WAIT_OBJECT_0;

    char messageModel[53];
    int messageSize = sizeof(messageModel) - 1;

    hDataFile = CreateFile("..\\..\\MainProcess\\DataLogger.txt",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDataFile == INVALID_HANDLE_VALUE)
    {
        printf("Falha ao abrir arquivo no Processo de Exibição de Dados! Código: %d.\n", GetLastError());
    }
    else printf("Arquivo de Exibição de Dados aberto com sucesso.\n");

    while (TRUE) {

        OEventStatus = WaitForSingleObject(hOEvent, 0);
        if (OEventStatus != WAIT_OBJECT_0 && awake) {
            awake = FALSE;
            printf("Thread DataDisplay bloqueando a pedido do usuario\n");
        }
        else if (OEventStatus == WAIT_OBJECT_0 && !awake) {
            awake = TRUE;
            printf("Thread DataDisplay acordando a pedido do usuario\n");
        }

        status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
        CheckForError(status);
        nTypeEvent = status - WAIT_OBJECT_0;

        if (nTypeEvent == 0) break;

        if (nTypeEvent == 1) {
            WaitForSingleObject(hDataWrittenEvent, INFINITE);
            WaitForSingleObject(hMutex, INFINITE);
            filePointer = SetFilePointer(hDataFile, nMessages * messageSize, NULL, FILE_BEGIN);
            fileStatus = ReadFile(hDataFile, &buffer, messageSize, &numberOfBytesToRead, NULL);
            nMessages = (nMessages + 1) % DISK_FILE_SIZE;
            ReleaseMutex(hMutex);
            if (fileStatus == 0) {
                printf("Falha no ReadFile de exibição de dados, código: (%d)\n", GetLastError());
            }
            else {
                message = messageFormater(buffer);
                cout << message << endl;
            }
            ResetEvent(hDataWrittenEvent);
        }
    }
    cout << "saí do while" << endl;
    CloseHandle(Events[0]);
    CloseHandle(Events[1]);
    CloseHandle(hDataWrittenEvent);
    CloseHandle(hMutex);
    printf("Processo que exibe dados encerrando sua execucao\n");
    return EXIT_SUCCESS;
}
