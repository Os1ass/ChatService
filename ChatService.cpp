#include "ChatService.h"
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_PORT  "27015"
#define BUFFER_SIZE   512

ChatService* ChatService::s_service = nullptr;

ChatService* ChatService::GetInstance()
{
    if (s_service == nullptr)
        s_service = new ChatService();
    return s_service;
}

ChatService::ChatService() : 
    m_workerThread(nullptr)
{
    s_service = this;
}

ChatService::~ChatService()
{
    CloseHandle(m_workerThread);
    WSACleanup();
}

BOOL ChatService::Init()
{
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) 
    {
        OutputDebugString(TEXT("Unable to startup WSA"));
        return FALSE;
    }

    m_workerThread = CreateThread(NULL, 0, WorkerThread, this, 0, NULL);
    if (m_workerThread == NULL)
    {
        OutputDebugString(TEXT("Unable to create thread"));
        return FALSE;
    }

    return TRUE;
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

    while (WaitForSingleObject(m_workerThread, 0) != WAIT_OBJECT_0) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }

        std::thread(&ChatService::ProcessClient, this, clientSocket).detach();
    }

    closesocket(listenSocket);
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
