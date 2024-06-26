#include "ChatService.h"
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

const std::string nicknameToMessageSeparator = ": ";
const BYTE magicNumber[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
const std::string magicNumberString(reinterpret_cast<const char*>(magicNumber), sizeof(magicNumber));
// magicNumber + message + magicNumber
// identifiation for socket send/recv

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

        
        std::string clientNickname;
        int iResult = RecieveMessageFromClient(clientSocket, clientNickname);
        if (iResult > 0) {
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

size_t ChatService::GetClients(std::string* clients)
{
    if (m_clientSocketsByNickname.size() == 0)
    {
        return 0;
    }

    clients = new std::string[m_clientSocketsByNickname.size()];
    int clientsSize = m_clientSocketsByNickname.size();
    auto client = m_clientSocketsByNickname.begin();
    for (int i = 0; i < clientsSize; i++, client++)
        clients[i] = client->first;
    return clientsSize;
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
    std::string recvStr;
    int iResult;
    while (!m_cancellationToken)
    {
        iResult = RecieveMessageFromClient(m_clientSocketsByNickname[clientNickname], recvStr);
        if (iResult > 0)
        {
            recvStr = clientNickname + nicknameToMessageSeparator + recvStr;
            SendToClients(recvStr);
        }
    }

    closesocket(m_clientSocketsByNickname[clientNickname]);
    m_clientSocketsByNickname.erase(clientNickname);
}

void ChatService::SendToClients(std::string message)
{
    std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
    for (auto client : m_clientSocketsByNickname)
    {
        SendMessageToClient(client.second, message);
    }
}

void ChatService::SendMessageToClient(SOCKET clientSocket, std::string message)
{
    std::string buffer = magicNumberString + message + magicNumberString;
    send(clientSocket, buffer.c_str(), buffer.length(), 0);
}

int ChatService::RecieveMessageFromClient(SOCKET clientSocket, std::string& message)
{
    char buffer[BUFFER_SIZE];
    int iResult = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (iResult <= magicNumberString.length() * 2 ||
        iResult <= 0)
    {
        return 0;
    }

    std::string bufferStr(buffer, iResult);
    if (bufferStr.substr(0, 4) != magicNumberString ||
        bufferStr.substr(bufferStr.length() - 4, 4) != magicNumberString) 
    {
        return 0;
    }

    message = bufferStr.substr(4, bufferStr.length() - 8);
    return iResult;
}