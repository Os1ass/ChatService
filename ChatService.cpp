#include "ChatService.h"
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

const std::string nicknameToMessageSeparator = ": ";

ChatService* ChatService::s_service = nullptr;

DWORD WINAPI ChatService::StaticRun(LPVOID lpParam)
{
    ChatService* service = (ChatService*)lpParam;
    service->Run();
    return 0;
}

void ChatService::Run()
{
    if (!Init())
        return;

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

    while (!m_cancellationToken)
    {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }

        char recvbuf[BUFFER_SIZE];
        int iResult = recv(clientSocket, recvbuf, BUFFER_SIZE, 0);
        if (iResult > 0) {
            std::string clientNickname(recvbuf, iResult);
            m_clientSocketsByNickname[clientNickname] = std::move(clientSocket);
            m_clientThreads.push_back(std::thread([this, clientNickname] { this->ProcessClient(clientNickname); }));
            std::string message = "Started thread for " + clientNickname;
            OutputDebugStringA(message.c_str());
        }
        else {
            closesocket(clientSocket);
        }
    }

    closesocket(listenSocket);
}

void ChatService::Stop()
{
    m_cancellationToken = true;
    for (auto& thread : m_clientThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_clientSocketsByNickname.clear();
    m_clientThreads.clear();
}

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
    m_cancellationToken = FALSE;
}

ChatService::~ChatService()
{
    Stop();
    WSACleanup();
}

BOOL ChatService::Init()
{
    m_cancellationToken = FALSE;

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        OutputDebugString(TEXT("Unable to startup WSA"));
        return FALSE;
    }

    return TRUE;
}

void ChatService::ProcessClient(std::string clientNickname)
{
    char recvbuf[BUFFER_SIZE];
    int iResult;
    while (!m_cancellationToken)
    {
        iResult = recv(m_clientSocketsByNickname[clientNickname], recvbuf, BUFFER_SIZE, 0);
        if (iResult > 0)
        {
            std::string recvstr(recvbuf, iResult);
            recvstr = clientNickname + nicknameToMessageSeparator + recvstr;
            SendToClients(recvstr.c_str(), iResult + clientNickname.length() + nicknameToMessageSeparator.length());
        }
    }

    closesocket(m_clientSocketsByNickname[clientNickname]);
    m_clientSocketsByNickname.erase(clientNickname);
}

void ChatService::SendToClients(const char* buf, int len)
{
    std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
    for (auto client : m_clientSocketsByNickname)
    {
        send(client.second, buf, len, 0);
    }
}
