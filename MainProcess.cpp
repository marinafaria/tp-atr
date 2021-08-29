/*
*  UFMG - ESCOLA DE ENGENHARIA
*  AUTOMAÇÃO EM TEMPO REAL - ELT127 - TD - 2021/1  
*  TRABALHO PRÁTICO: Detecção de Falhas e Alarmes em Planta Industrial de Papel 
* 
*  Davi de Paula Cardoso - 2017001958
*  Marina Faria - 2017074580
* 
*  Descrição:
* 
*/

#define WIN32_LEAN_AND_MEAN
#define HAVE_STRUCT_TIMESPEC
#define __WIN32_WINNT 0x500

#include <iostream>
#include <string>
#include <list> 
#include <thread>
#include <windows.h>
#include <stdlib.h>
#include <conio.h>
#include <process.h>				
#include <stdio.h>
#include <strsafe.h>
#include <ctgmath>
#include <math.h>
#define CHECKERROR    1    // ativa função CheckForError
#include "CheckForError.h"

using namespace std;

typedef unsigned (WINAPI* CAST_FUNCTION)(LPVOID);
typedef unsigned* CAST_LPDWORD;

// Definição de Constantes
#define SIZE_LIST		100
#define	ESC				0x1B
#define	A   	        0x61
#define	C		        0x63
#define	D		        0x64
#define	O		        0x6F
#define	P		        0x70
#define	S     	        0x73
#define M				0x6D
#define NUM_THREADS	    5
#define TEMP_TIME		500
#define _WIN32_WINNT	0x0400
#define DISK_FILE_SIZE	200

DWORD WINAPI threadReadSDCD(); //Thread responsável pela Leitura de mensagens provenientes do SDCD. Deposita elas na lista circular.
DWORD WINAPI threadReadPIMS(); //Thread responsável pela Leitura de mensagens provenientes do PIMS. Deposita elas na lista circular.
DWORD WINAPI threadGetData(); //Thread responsável por capturar mensagens de dados da thread de leitura, tanto para alarmes quanto dados.
DWORD WINAPI threadGetAlarm(); //Thread responsável por capturar mensagens de dados da thread de leitura, tanto para alarmes quanto dados.
DWORD WINAPI threadReadKeyboard(); //Thread responsável por ler as entradas do usuário

// Objetos de sincronização
HANDLE hMutex; // Mutex de proteçao da lista

HANDLE hEscEvent; // Evento que sinaliza o termino da execução
HANDLE hAEvent; // Evento que bloqueia/libera a thread de captura de alarmes  -> threadGetAlarm
HANDLE hCEvent; // Evento que bloqueia/libera a thread de exibição de alarmes -> ProcessoExibeAlarme
HANDLE hDEvent; // Evento que bloqueia/libera a thread de captura de dados do processo -> threadGetData
HANDLE hOEvent; // Evento que bloqueia/libera a thread de exibição de dados do processo -> ProcessoExibeDado
HANDLE hPEvent; // Evento que bloqueia/libera a thread de leitura do PIMS  -> threadReadPIMS
HANDLE hSEvent; // Evento que bloqueia/libera a thread de leitura do SDCD -> threadReadSDCD
HANDLE hDataWrittenEvent; // Sinaliza a thread de captura de dados existe dado(s) a ser(em) lido(s)

HANDLE hSDCDTimer; // Temporizador para a geração de mensagens SDCD
HANDLE hUncriticalAlarmTimer; // Temporizador para a geração de alarmes não críticos do PIMS
HANDLE hCriticalAlarmTimer; // Temporizador para a geração de alarmes críticos do PIMS

HANDLE availableSpot; // Semáforo que sinaliza para as threads de leitura SDCD e PIMS que podem inserir na lista
HANDLE newSDCDInserted; // Alarme que sinaliza para a thread de captura de SDCD que um novo dado foi inserido na lista
HANDLE newAlarmInserted; // Alarme que sinaliza para a thread de captura de Alarme que um novo alarme foi inserido na lista

HANDLE hAlarmMailslot; // Maislot para os alarmes
LPCTSTR alarmMailslotName = TEXT("\\\\.\\mailslot\\AlarmDisplay");
HANDLE hAlarmFile; // Arquivo que armazena as mensagens de alarmes
HANDLE hAlarmMailslotEvent;

int nKey; // variável para armazenar a tecla pressionada
list <string> circularList; // lista circular, recurso compartilhado entre as threads
int nseq = 0;

// FUNÇÕES DE SUPORTE
// Preeche uma string com uma determinada quantidade de zeros
string zeroFiller(int number, int digits, bool left = true) {
	string numString = to_string(number);
	int nZeros = digits - numString.length();

	for (int i = 0; i < nZeros; i++) {
		numString = left ? "0" + numString : numString + "0";
	}

	return(numString);
}

// Gera um alfanumerico aleatorio com a quantidade de letras desejada, seguido de 4 números inteiros. Um hifen pode ser adicionado
string alphaNumericGenerator(int nChar, bool hyphen = false) {
	string alphaNumeric = "";
	string alphaNumericAux = "";

	for (int i = 0; i < nChar; i++) {
		alphaNumeric += 'A' + rand() % 26;
	}

	alphaNumericAux += 'A' + rand() % 26;
	
	int number1 = rand() % 99 + 1;
	int number2 = rand() % 99 + 1;

	alphaNumeric = alphaNumeric + "-" + zeroFiller(number1, 2) + "-" + alphaNumericAux + zeroFiller(number2, 2);

	return alphaNumeric;
}

// Retorna uma string com a hora local
string localTime() {
	SYSTEMTIME time;
	GetLocalTime(&time);
	return zeroFiller(time.wHour, 2) + ":" + zeroFiller(time.wMinute, 2) + ":" + zeroFiller(time.wSecond, 2) + ":" + zeroFiller(time.wMilliseconds, 3, false);
}

// Gera um float e o transforma para string
string RealNumberToString(int maxNumber) {
	float number = rand() / (RAND_MAX / (float)maxNumber);
	string numStr = to_string(number);

	// Limita o numero a duas casas decimais
	int dotPosition = numStr.find(".");
	numStr.erase(numStr.begin() + dotPosition + 3, numStr.end());

	//Adiciona zeros a esquerda para completar 4 dígitos    
	int nZeros = 8 - numStr.length();
	for (int i = 0; i < nZeros; i++) {
		numStr = "0" + numStr;
	}

	return numStr;
}

// Gera um float e o transforma para string
string RealRandNumber(int maxNumber, int zerosNumber) {
	int number = rand() / (RAND_MAX / (float)maxNumber);
	string numStr = to_string(number);

	//Adiciona zeros a esquerda para completar 4 dígitos    
	int nZeros = zerosNumber - numStr.length();
	for (int i = 0; i < nZeros; i++) {
		numStr = "0" + numStr;
	}

	return numStr;
}

// Retorna uma Engeneering Unit aleatória  
string GiveEU() {
	string EUs[4] = { "K", "Kgf/m2", "Kg/m3", "m/s" };
	int number = rand() % 4 ;
	string chosenEU = EUs[number];

	int nZeros = 8 - chosenEU.length();
	for (int i = 0; i < nZeros; i++) {
		chosenEU = chosenEU + " ";
	}

	return chosenEU;
}

// Define aleatoriamente qual modo a mensagem terá Automático ou Manual
string DefineMode() {
	string modes[2] = { "A", "M" };
	int number = rand() % 2;
	string chosenMode = modes[number];

	return chosenMode;

}

// Gera a mensagem de dado do processo
string createSDCDMessage() {
	string message;
	nseq == 999999 ? nseq = 1 : nseq++;
	string value = RealNumberToString(99999);
	string EU = GiveEU();
	string mode = DefineMode();
	message = zeroFiller(nseq, 6) + "|1|" + alphaNumericGenerator(3, true) + "|" + value + "|" + EU + "|" + mode + "|" + localTime();
	return(message);
}

// Gera a mensagem de alarme do PIMS
string createPIMSMessage(int type) {
	string message;
	string typeSelected = to_string(type);
	nseq == 999999 ? nseq = 1 : nseq++;
	string alarmID = RealRandNumber(9999, 4);
	string degree = RealRandNumber(99, 2);
	string prev = RealRandNumber(14440, 5);
	message = zeroFiller(nseq, 6) + "|" + typeSelected + "|"  +  alarmID + "|" + degree + "|" +  prev + "|" + localTime();
	return message;
}

// Retorna a primeira mensagem da lista do tipo esperado
string getMessage(list <string> message, int type) {
	list <string> ::iterator it;
	char messageType = type + '0';
	string finalMessage;
	for (it = message.begin(); it != message.end(); ++it) {
		finalMessage = *it;
		if (sizeof(finalMessage ) > 6) {
			if (finalMessage[7] == messageType) break; // CONFERIR SE É 7 OU SE É 6 PARA SABER O TIPO
		}
	}

	if (it == message.end()) return ""; //Nenhuma mensagem do tipo especificado foi encontrada
	return finalMessage;
}

LONG getUncriticalTime() {
	srand((unsigned)time(0));
	return ((rand() % 40) + 10)*(pow(10, 6)); // retorna entre 1 e 5 segundos
}

LONG getCriticalTime() {
	srand((unsigned)time(0));
	return ((rand() % 70) + 30) * (pow(10, 6)); // retorna entre 3 e 8 segundos
}

int main()
{
	HANDLE hThreads[NUM_THREADS];
    DWORD dwReadSDCD, dwReadPIMS, dwGetData, dwGetAlarm, dwReadKeyboard;
	DWORD dwRet;

	newSDCDInserted = CreateSemaphore(NULL, 0, SIZE_LIST, "NewSDCDonList"); // Semáforo responsável por indicar para a thread getData que uma nova mensagem foi depositada na lista
	CheckForError(newSDCDInserted);
	newAlarmInserted = CreateSemaphore(NULL, 0, SIZE_LIST, "NewAlarmonList");
	CheckForError(newAlarmInserted);
	availableSpot = CreateSemaphore(NULL, SIZE_LIST, SIZE_LIST, "PosicaoLivre"); // Semáforo responsável por indicar que há uma posição livre na lista
	CheckForError(availableSpot);
	hEscEvent = CreateEvent(NULL, TRUE, FALSE, "EscEvent");
	CheckForError(hEscEvent);
	hSEvent = CreateEvent(NULL, TRUE, TRUE, "SEvent"); // Evento de controle da geração de dados do SDCD
	CheckForError(hSEvent);
	hDEvent = CreateEvent(NULL, TRUE, TRUE, "DEvent"); // Evento de controle da captura de dados do SDCD
	CheckForError(hDEvent);
	hPEvent = CreateEvent(NULL, TRUE, TRUE, "PEvent"); // Evento de controle da geração de alarmes do PIMS
	CheckForError(hPEvent);
	hAEvent = CreateEvent(NULL, TRUE, TRUE, "AEvent"); // Evento de controle da captura de Alarmes do PIMS
	CheckForError(hAEvent);
	hOEvent = CreateEvent(NULL, TRUE, TRUE, "OEvent"); // Evento de exibição de dados do SDCD
	CheckForError(hOEvent);
	hCEvent = CreateEvent(NULL, TRUE, TRUE, "CEvent"); // Evento de exibição de dados do SDCD
	CheckForError(hCEvent);
	hDataWrittenEvent = CreateEvent(NULL, TRUE, FALSE, "DataWrittenEvent");
	CheckForError(hDataWrittenEvent);
	hMutex = CreateMutex(NULL, FALSE, "hMutex");
	CheckForError(hMutex);
	hAlarmMailslotEvent = CreateEvent(NULL, TRUE, FALSE, "hAlarmMailslotEvent");
	CheckForError(hAlarmMailslotEvent);

	// Criacao dos processos de exibição de dados e defeitos
	BOOL stats;
	STARTUPINFO process1, process2;
	PROCESS_INFORMATION AlarmDisplayProcess;
	PROCESS_INFORMATION DataDisplayProcess;

	ZeroMemory(&process1, sizeof(process1));
	process1.cb = sizeof(process1);
	stats = CreateProcess(
		"Debug\\AlarmDisplayProcess.exe",
		NULL,
		NULL,
		NULL,
		FALSE,
		CREATE_NEW_CONSOLE,
		NULL,
		"Debug",
		&process1,
		&AlarmDisplayProcess);
	if (!stats)
		cout << "Erro na criacao do processo de Exibicao de Alarmes!  ERRO: " << GetLastError() << endl;
	else
		cout << "Processo de Exibicao de Alarmes criado com sucesso! " << endl;

	ZeroMemory(&process2, sizeof(process2));
	process2.cb = sizeof(process2);
	stats = CreateProcess(
		"Debug\\DataDisplayProcess.exe",
		NULL,
		NULL,
		NULL,
		FALSE,
		CREATE_NEW_CONSOLE,
		NULL,
		"Debug",
		&process2,
		&DataDisplayProcess);
	if (!stats)
		cout << "Erro na criacao do processo de Exibicao de Dados!  ERRO: " << GetLastError() << endl;
	else
		cout << "Processo de Exibicao de dados criado com sucesso! " << endl;


	// Criacao dos arquivos
	WaitForSingleObject(hAlarmMailslotEvent, INFINITE);
	hAlarmFile = CreateFile(alarmMailslotName,
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		(LPSECURITY_ATTRIBUTES)NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		(HANDLE)NULL);

	if (hAlarmFile == INVALID_HANDLE_VALUE)
	{
		printf("Falha ao criar arquivo de alarmes! Código: %d.\n", GetLastError());
		exit(0);
	}
	else printf("Arquivo de alarmes criado com sucesso.\n");

	// Criação dos timers

	hSDCDTimer = CreateWaitableTimer(NULL, FALSE, "TimerSDCD");
	if (NULL == hSDCDTimer)
	{
		printf("Falha no CreateWaitableTimer do leitor de SDCD (%d)\n", GetLastError());
		return 1;
	}

	hUncriticalAlarmTimer = CreateWaitableTimer(NULL, FALSE, "TimerUncriticalPIMS");
	if (NULL == hUncriticalAlarmTimer)
	{
		printf("Falha no CreateWaitableTimer na mensagem não crítica do PIMS (%d)\n", GetLastError());
		return 1;
	}

	hCriticalAlarmTimer = CreateWaitableTimer(NULL, FALSE, "TimerCriticalPIMS");
	if (NULL == hCriticalAlarmTimer)
	{
		printf("Falha no CreateWaitableTimer na mensagem crítica do PIMS (%d)\n", GetLastError());
		return 1;
	}

	void* nomeThreads[NUM_THREADS] = { threadReadSDCD, threadReadPIMS, threadGetData, threadGetAlarm, threadReadKeyboard };
	string stringNomeThreads[NUM_THREADS] = { "threadReadSDCD", "threadReadPIMS", "threadGetData", "threadGetAlarm", "threadReadKeyboard"};
	DWORD* nomeIds[NUM_THREADS] = { &dwReadSDCD, &dwReadPIMS, &dwGetData, &dwGetAlarm, &dwReadKeyboard };

	for (int i = 0; i < NUM_THREADS; ++i) {
		hThreads[i] = (HANDLE)_beginthreadex(
			NULL,
			0,
			(CAST_FUNCTION)(nomeThreads[i]),
			(LPVOID)0,
			0,
			(CAST_LPDWORD)(nomeIds[i]));
		if (hThreads[i])
			cout << "Thread " << stringNomeThreads[i] << " criada com sucesso!" << endl;
		else {
			cout << "Erro na criacao da " << stringNomeThreads[i] <<  " ! N =" << i << "Erro =" << errno << endl;
			exit(0);
		}
	}


	// Aguarda finalizacao
	printf("Esperando encerramento das threads\n" );

	dwRet = WaitForMultipleObjects(5, hThreads, TRUE, INFINITE);
	CheckForError(dwRet);

	// Fecha todos os handles de objetos do kernel
	for (int i = 0; i < 5; ++i)
		CloseHandle(hThreads[i]);

	CloseHandle(DataDisplayProcess.hProcess);
	CloseHandle(DataDisplayProcess.hThread);
	CloseHandle(AlarmDisplayProcess.hProcess);
	CloseHandle(AlarmDisplayProcess.hThread);

	CloseHandle(availableSpot);
	CloseHandle(newSDCDInserted);
	CloseHandle(newAlarmInserted);
	CloseHandle(hEscEvent);
	CloseHandle(hAEvent);
	CloseHandle(hCEvent);
	CloseHandle(hDEvent);
	CloseHandle(hOEvent);
	CloseHandle(hPEvent);
	CloseHandle(hSEvent);
	CloseHandle(hDataWrittenEvent);
	CloseHandle(hMutex);
	CloseHandle(hSDCDTimer);
	CloseHandle(hUncriticalAlarmTimer);
	CloseHandle(hCriticalAlarmTimer);
	CloseHandle(hAlarmFile);
	CloseHandle(hAlarmMailslotEvent);
	CloseHandle(hAlarmMailslot);

}

DWORD WINAPI threadReadSDCD() {
	HANDLE Events[3] = { hEscEvent, hSEvent, hSDCDTimer }; // Eventos que a thread vai receber
	DWORD status;
	DWORD statusMutex;
	int nEventType;

	LARGE_INTEGER SDCDtime;
	SDCDtime.QuadPart = -10000000;

	status = SetWaitableTimer(hSDCDTimer, &SDCDtime, TEMP_TIME, NULL, NULL, 0);
	if (status == 0) printf("Falha no SetWaitableTimer, código: (%d)\n", GetLastError());

		
	while (TRUE) {
		status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
		CheckForError(status);
		nEventType = status - WAIT_OBJECT_0;
		
		if (nEventType == 0) break; // Se Esc é pressionado encerra -> Sai do while
		else { 		
			HANDLE waitForEvents[3] = { hEscEvent, availableSpot, hSEvent }; // Espera
			string SDCDmessage = createSDCDMessage(); // Chama função que cria uma mensagem no formato SDCD
			// Aguarda uma posiçao para depositar a mensagem
			statusMutex = WaitForSingleObject(hMutex, INFINITE);
			CheckForError(statusMutex == WAIT_OBJECT_0);
			if (circularList.size() == SIZE_LIST) {
				printf("Aguardando nova posicao na lista...\n" ); 
			}
			statusMutex = ReleaseMutex(hMutex);
			CheckForError(statusMutex);

			status = WaitForMultipleObjects(3, waitForEvents, FALSE, INFINITE);
			CheckForError(status);
			nEventType = status - WAIT_OBJECT_0;

			if (nEventType == 1) {
				WaitForSingleObject(hMutex, INFINITE);
				circularList.push_back(SDCDmessage);
				printf("threadReadSDCD: Nova mensagem inserida na lista: %s \n", SDCDmessage.c_str()); 
				statusMutex = ReleaseMutex(hMutex);
				CheckForError(statusMutex);
				status = ReleaseSemaphore(newSDCDInserted, 1, NULL); //Sinaliza q inseriu um novo dado do tipo SDCD na lista
				CheckForError(status);
			}
		}
		WaitForSingleObject(hSDCDTimer, INFINITE);
	}
	
	printf("Thread threadReadSDCD encerrando execucao\n");
	return(0);
	_endthreadex(0);
}

DWORD WINAPI threadReadPIMS() {
	HANDLE Events[2] = { hEscEvent, hPEvent }; // Eventos que a thread vai receber
	DWORD status;
	DWORD statusMutex;
	DWORD numberOfBytesWritten;
	DWORD nTimerType;
	int nEventType;
	HANDLE timers[2] = { hUncriticalAlarmTimer , hCriticalAlarmTimer };
	string uncriticalPIMSMessage;

	LARGE_INTEGER uncriticalTime;
	uncriticalTime.QuadPart = getUncriticalTime() * (-1);
	status = SetWaitableTimer(hUncriticalAlarmTimer, &uncriticalTime, 0, NULL, NULL, 0);
	if (status == 0) printf("Falha no SetWaitableTimer não crítico, código: (%d)\n", GetLastError());

	LARGE_INTEGER criticalTime;
	criticalTime.QuadPart = getCriticalTime() * (-1);
	status = SetWaitableTimer(hCriticalAlarmTimer, &criticalTime, 0, NULL, NULL, 0);
	if (status == 0) printf("Falha no SetWaitableTimer crítico, código: (%d)\n", GetLastError());

	while (TRUE) {
		status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
		CheckForError(status);
		nEventType = status - WAIT_OBJECT_0;

		if (nEventType == 0) break; // Se Esc é pressionado encerra -> Sai do while
		else {
			HANDLE waitForEvents[3] = { hEscEvent, availableSpot, hPEvent }; // Espera

			nTimerType = WaitForMultipleObjects(2, timers, FALSE, INFINITE);
			if (nTimerType == (WAIT_OBJECT_0 + 0)) {
				uncriticalPIMSMessage = createPIMSMessage(2); // Chama função que cria uma mensagem no formato PIMS não critico
				uncriticalTime.QuadPart = getUncriticalTime() * (-1);
				status = SetWaitableTimer(hUncriticalAlarmTimer, &uncriticalTime, 0, NULL, NULL, 0);
				if (status == 0) printf("Falha no SetWaitableTimer não crítico, código: (%d)\n", GetLastError());
			}
			else if (nTimerType == (WAIT_OBJECT_0 + 1)) {
				string criticalPIMSmessage = createPIMSMessage(9); // Chama função que cria uma mensagem no formato PIMS critico
				status = WriteFile(hAlarmFile, criticalPIMSmessage.c_str(), sizeof(char) * (criticalPIMSmessage.length() + 1), &numberOfBytesWritten, NULL);
				if (status == 0) printf("Falha no WriteFile de alarmes, código: (%d)\n", GetLastError());
				//printf(criticalPIMSmessage.c_str());
				uncriticalTime.QuadPart = getCriticalTime() * (-1);
				status = SetWaitableTimer(hCriticalAlarmTimer, &criticalTime, 0, NULL, NULL, 0);
				if (status == 0) printf("Falha no SetWaitableTimer crítico, código: (%d)\n", GetLastError());
			}
			else {
				printf("Erro no WaitForMultipleObjects de timers do PIMS! Código: %d\n", GetLastError());
				ExitProcess(0);
			}

			// Aguarda uma posiçao para depositar a mensagem
			statusMutex = WaitForSingleObject(hMutex, INFINITE);
			CheckForError(statusMutex == WAIT_OBJECT_0);
			if (circularList.size() == SIZE_LIST) {
				printf("Aguardando nova posicao na lista...\n");
			}
			statusMutex = ReleaseMutex(hMutex);
			CheckForError(statusMutex);

			status = WaitForMultipleObjects(2, waitForEvents, FALSE, INFINITE);
			CheckForError(status);
			nEventType = status - WAIT_OBJECT_0;
			
			if (nEventType == 1) {
				statusMutex = WaitForSingleObject(hMutex, INFINITE);
				CheckForError(statusMutex == WAIT_OBJECT_0);
				circularList.push_back(uncriticalPIMSMessage);
				statusMutex = ReleaseMutex(hMutex);
				CheckForError(statusMutex);
				status = ReleaseSemaphore(newAlarmInserted, 1, NULL); //Sinaliza q inseriu um novo alarme na lista
				CheckForError(status);
			}
		}
	}

	cout << "Thread threadReadPIMS encerrando execucao\n" << endl;
	return(0);
	_endthreadex(0);
}

DWORD WINAPI threadGetData() {
	HANDLE Events[2] = { hEscEvent, hDEvent }; // Exemplo de eventos que a thread vai receber
	DWORD status;
	DWORD statusMutex;
	DWORD statusEvent;
	DWORD numberOfBytesWritten;
	int nEventType;
	LONG filePointer;
	HANDLE hDataFile;
	int nMessages = 0; // Armazena o número de mensagens escritas no arquivo circular em disco

	hDataFile = CreateFile("DataLogger.txt",
		GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		(LPSECURITY_ATTRIBUTES)NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		(HANDLE)NULL);
		
	if (hDataFile == INVALID_HANDLE_VALUE)
	{
		printf("Falha ao criar arquivo de captura de dados! Código: %d.\n", GetLastError());
		exit(0);
	}
	else printf("Arquivo de captura de dados criado com sucesso.\n");

	while (TRUE) {
		status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
		CheckForError(status);
		nEventType = status - WAIT_OBJECT_0;

		if (nEventType == 0) break; // Se Esc é pressionado encerra a execução
		else {
			HANDLE Events[3] = { hEscEvent, newSDCDInserted, hDEvent }; // Exemplo de eventos que a thread vai receber
			status = WaitForMultipleObjects(3, Events, FALSE, INFINITE);
			CheckForError(status);
			nEventType = status - WAIT_OBJECT_0;
						
			if (nEventType == 1 && WaitForSingleObject(hDEvent, 0) == WAIT_OBJECT_0 ) {

				// Pega a mensagem da lista circular
				statusMutex = WaitForSingleObject(hMutex, INFINITE);
				CheckForError(statusMutex == WAIT_OBJECT_0);
				string SDCDmessage = getMessage(circularList, 1);
				statusMutex = ReleaseMutex(hMutex);
				CheckForError(statusMutex);
				

				if (!SDCDmessage.empty()) {
					printf("threadGetData: Nova mensagem retirada da lista: %s \n", SDCDmessage.c_str());
					int SDCDMessageSize = sizeof(char) * SDCDmessage.length();
					filePointer = SetFilePointer(hDataFile, nMessages * SDCDMessageSize, NULL, FILE_BEGIN);
					statusEvent = WaitForSingleObject(hOEvent, 0);
					if (nMessages == 0 && statusEvent != WAIT_OBJECT_0) { // significa que o arquivo está cheio e não pode retirar mensagens
						printf("Aguardando espaco no arquivo de dados do processo... \n");
						WaitForSingleObject(hOEvent, INFINITE);
					}
					statusMutex = WaitForSingleObject(hMutex, INFINITE);
					status = WriteFile(hDataFile, SDCDmessage.c_str(), SDCDMessageSize, &numberOfBytesWritten, NULL);
					if (status != 0) nMessages = (nMessages + 1) % DISK_FILE_SIZE;
					SetEvent(hDataWrittenEvent);
					//Retira a mensagem da lista circular
					CheckForError(statusMutex == WAIT_OBJECT_0);
					circularList.remove(SDCDmessage);
					statusMutex = ReleaseMutex(hMutex);
					CheckForError(statusMutex);
					status = ReleaseSemaphore(availableSpot, 1, NULL); // Libera posicao na lista
					CheckForError(status);
				}
			}
		}
	}

	cout << "Thread threadGetData encerrando sua execucao\n" << endl;
	CloseHandle(hDataFile);
	return(0);
	_endthreadex(0);
}

DWORD WINAPI threadGetAlarm() {
	HANDLE Events[2] = { hEscEvent, hAEvent }; // Exemplo de eventos que a thread vai receber
	DWORD status;
	DWORD statusMutex;
	int nEventType;
	DWORD numberOfBytesWritten;

	while (TRUE) {
		status = WaitForMultipleObjects(2, Events, FALSE, INFINITE);
		CheckForError(status);
		nEventType = status - WAIT_OBJECT_0;

		if (nEventType == 0) break; // Se Esc é pressionado encerra a execução
		else {
			HANDLE Events[3] = { hEscEvent, newAlarmInserted, hAEvent }; // Exemplo de eventos que a thread vai receber
			status = WaitForMultipleObjects(3, Events, FALSE, INFINITE);
			CheckForError(status);
			nEventType = status - WAIT_OBJECT_0;

			if (nEventType == 1) {

				// Pega a mensagem da lista circularWaitForSingle
				statusMutex = WaitForSingleObject(hMutex, INFINITE);
				CheckForError(statusMutex == WAIT_OBJECT_0);
				string uncriticalPIMSMessage = getMessage(circularList, 2);
				status = WriteFile(hAlarmFile, uncriticalPIMSMessage.c_str(), sizeof(char) * (uncriticalPIMSMessage.length() + 1), &numberOfBytesWritten, NULL);
				if (status == 0) printf("Falha no WriteFile de alarmes, codigo: (%d)\n", GetLastError());
				statusMutex = ReleaseMutex(hMutex);
				CheckForError(statusMutex);


				if (!uncriticalPIMSMessage.empty()) {
					printf("threadGetAlarms: Novo alarme retirado da lista: %s \n", uncriticalPIMSMessage.c_str());
					//Retira a mensagem da lista circular
					statusMutex = WaitForSingleObject(hMutex, INFINITE);
					CheckForError(statusMutex == WAIT_OBJECT_0);
					circularList.remove(uncriticalPIMSMessage);
					statusMutex = ReleaseMutex(hMutex);
					CheckForError(statusMutex);
					status = ReleaseSemaphore(availableSpot, 1, NULL); // Libera posicao na lista
					CheckForError(status);
				}
			}
		}
	}

	cout << "Thread threadGetAlarm encerrando execucao\n" << endl;
	return(0);
	_endthreadex(0);
}

DWORD WINAPI threadReadKeyboard() {
	// Variaveis para guardar o estado do evento (sinalizado ou nao sinalizado)
	bool sAwake = TRUE;
	bool dAwake = TRUE;
	bool pAwake = TRUE;
	bool aAwake = TRUE;
	bool cAwake = TRUE;
	bool oAwake = TRUE;

	do {
		nKey = _getch();
		
		if (nKey == S) { // Bloqueia/Desbloqueia thread de leitura do SDCD
			sAwake ? ResetEvent(hSEvent) : SetEvent(hSEvent);
			sAwake ? printf("Bloqueando thread de leitura do SDCD \n") : printf("Desbloqueando thread de leitura do SDCD \n");
			sAwake = !sAwake;
		}

		if (nKey == D) { // Bloqueia/Desbloqueia thread de captura de dados do processo
			dAwake ? ResetEvent(hDEvent) : SetEvent(hDEvent);
			dAwake ? printf("Bloqueando thread de captura de dados do processo \n") : printf("Desbloqueando thread de captura de dados do processo \n");
			dAwake = !dAwake;
		}

		if (nKey == P) { // Bloqueia/Desbloqueia thread de leitura do PIMS
			pAwake ? ResetEvent(hPEvent) : SetEvent(hPEvent);
			pAwake ? printf("Bloqueando thread de leitura do PIMS \n") : printf("Desbloqueando thread de leitura do PIMS \n");
			pAwake = !pAwake;
		}

		if (nKey == A) { // Bloqueia/Desbloqueia thread de captura dos alarmes
			aAwake ? ResetEvent(hAEvent) : SetEvent(hAEvent);
			aAwake ? printf("Bloqueando thread de captura dos alarmes \n") : printf("Desbloqueando thread de captura dos alarmes \n");

			aAwake = !aAwake;
		}

		if (nKey == C) { // Bloqueia/Desbloqueia o processo de exibição de alarmes
			cAwake ? ResetEvent(hCEvent) : SetEvent(hCEvent);
			cAwake = !cAwake;
		}

		if (nKey == O) { // Bloqueia/Desbloqueia o processo de exibição de dados do processo
			oAwake ? ResetEvent(hOEvent) : SetEvent(hOEvent);
			oAwake = !oAwake;
		}

		if (nKey == M) { //Mostra o menu
			printf("Tecle alguma das letras abaixo: \n");
			printf("<S> - Bloquear ou retomar leitura do SDCD \n");
			printf("<P> - Bloquear ou retomar leitura do PIMS \n");
			printf("<D> - Bloquear ou retomar a captura de dados do processo \n");
			printf("<A> - Bloquear ou retomar a captura de alarmes \n");
			printf("<O> - Bloquear ou retomar a exibicao de dados do processo \n");
			printf("<C> - Bloquear ou retomar a exibicao de alarmes \n");
			printf("<M> - Exibir o menu \n");
			printf("<ESC> - Encerrar aplicacao\n");
		}

	} while (nKey != ESC);
	SetEvent(hEscEvent);

	printf("Thread threadReadKeyboard encerrando execucao\n" );
	return(0);
	_endthreadex(0);
}




