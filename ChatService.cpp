#include "ChatService.h"
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

#define SERVICE_NAME  TEXT("ChatService")
#define DEFAULT_PORT  "27015"
#define BUFFER_SIZE   512

ChatService* ChatService::s_service = nullptr;

ChatService::ChatService() :
    m_serviceStatusHandle(nullptr),
    m_stopEvent(INVALID_HANDLE_VALUE),
    m_workerThread(nullptr)
{
    s_service = this;
}

ChatService::~ChatService()
{
    if (m_stopEvent != INVALID_HANDLE_VALUE)
        CloseHandle(m_stopEvent);
    if (m_workerThread)
        CloseHandle(m_workerThread);
    WSACleanup();
}

BOOL ChatService::Run()
{
    SERVICE_TABLE_ENTRY serviceTable[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMainWrapper },
        { NULL, NULL }
    };

    return StartServiceCtrlDispatcher(serviceTable);
}

void ChatService::Stop()
{
    if (m_stopEvent != INVALID_HANDLE_VALUE)
        SetEvent(m_stopEvent);
}

BOOL ChatService::Init()
{
    m_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (m_stopEvent == NULL)
        return FALSE;

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return FALSE;
    }

    return TRUE;
}

void WINAPI ChatService::ServiceMainWrapper(DWORD dwArgc, LPTSTR* lpszArgv)
{
    if (s_service) {
        s_service->ServiceMain(dwArgc, lpszArgv);
    }
}

void ChatService::ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv)
{
    m_serviceStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!m_serviceStatusHandle) {
        return;
    }

    m_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    m_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    m_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    m_serviceStatus.dwWin32ExitCode = 0;
    m_serviceStatus.dwServiceSpecificExitCode = 0;
    m_serviceStatus.dwCheckPoint = 0;
    m_serviceStatus.dwWaitHint = 0;

    if (!Init()) {
        m_serviceStatus.dwCurrentState = SERVICE_STOPPED;
        m_serviceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(m_serviceStatusHandle, &m_serviceStatus);
        return;
    }

    m_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(m_serviceStatusHandle, &m_serviceStatus);

    m_workerThread = CreateThread(NULL, 0, WorkerThread, this, 0, NULL);
    WaitForSingleObject(m_stopEvent, INFINITE);

    m_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(m_serviceStatusHandle, &m_serviceStatus);
}

void ChatService::WorkerThreadImpl()
{
    struct addrinfo* result = NULL, hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    int iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        return;
    }

    SOCKET listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listenSocket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return;
    }

    iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        freeaddrinfo(result);
        closesocket(listenSocket);
        return;
    }

    freeaddrinfo(result);

    iResult = listen(listenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        closesocket(listenSocket);
        return;
    }

    while (WaitForSingleObject(m_stopEvent, 0) != WAIT_OBJECT_0) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }

        std::thread(&ChatService::ProcessClient, this, clientSocket).detach();
    }

    closesocket(listenSocket);
}

void WINAPI ChatService::ServiceCtrlHandler(DWORD dwCtrl)
{
    switch (dwCtrl)
    {
    case SERVICE_CONTROL_STOP:
        if (s_service) {
            s_service->Stop();
        }
        break;
    default:
            break;
    }
}

DWORD WINAPI ChatService::WorkerThread(LPVOID lpParam)
{
    ChatService* pService = (ChatService*)lpParam;
    pService->WorkerThreadImpl();
    return 0;
}

void ChatService::ProcessClient(SOCKET clientSocket)
{
    char recvbuf[BUFFER_SIZE];
    int iResult;
    do {
        iResult = recv(clientSocket, recvbuf, BUFFER_SIZE, 0);
        if (iResult > 0) {
            std::string modifiedMessage = "[Server Received]: " + std::string(recvbuf, iResult);
            send(clientSocket, modifiedMessage.c_str(), modifiedMessage.length(), 0);
        }
        else if (iResult == 0) {
            break;
        }
    } while (iResult > 0);

    closesocket(clientSocket);
}
