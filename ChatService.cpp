#include "ChatService.h"
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

const std::string g_nicknameToMessageSeparator = ": ";
extern const BYTE g_magicNumber[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
extern const std::string g_magicNumberString(reinterpret_cast<const char*>(g_magicNumber), sizeof(g_magicNumber));
// magicNumber + message + magicNumber
// identifiation for socket send/recv and pipe

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
        if (m_clientsByNickname.find(clientNickname) != m_clientsByNickname.end())
        {
            SendToClient(clientSocket, "Can't connect to the server, name " + clientNickname + " already in use");
            closesocket(clientSocket);
            continue;
        }

        if (iResult > 0) {
            SendToClients(clientNickname + " connected, say hello!");
            std::string message = "Starting thread for " + clientNickname;
            OutputDebugStringA(message.c_str());
            std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
            m_clientsByNickname[clientNickname] = std::move(client(clientSocket, std::thread([this, clientNickname] { this->ProcessClient(clientNickname); })));
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
    for (auto& client : m_clientsByNickname) {
        if (client.second.thread.joinable()) {
            client.second.thread.join();
        }
    }
    m_clientsByNickname.clear();
}

ChatService* ChatService::GetInstance()
{
    if (s_service == nullptr)
        s_service = new ChatService();
    return s_service;
}

size_t ChatService::GetClients(std::string*& clients)
{
    std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
    auto client = m_clientsByNickname.begin();
    while (client != m_clientsByNickname.end())
    {
        if (client->second.socket == INVALID_SOCKET)
        {
            client->second.thread.join();
            client = m_clientsByNickname.erase(client);
        }
        else
        {
            client++;
        }
    }
    if (m_clientsByNickname.size() == 0)
    {
        return 0;
    }

    int clientsSize = m_clientsByNickname.size();
    clients = new std::string[clientsSize];
    client = m_clientsByNickname.begin();
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
        iResult = RecieveMessageFromClient(m_clientsByNickname[clientNickname].socket, recvStr);
        if (iResult > 0)
        {
            recvStr = clientNickname + g_nicknameToMessageSeparator + recvStr;
            SendToClients(recvStr);
        } 
        else
        if (iResult == -1)
        {
            break;
        }
    }
    
    std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
    closesocket(m_clientsByNickname[clientNickname].socket);
    m_clientsByNickname[clientNickname].socket = INVALID_SOCKET;
}

void ChatService::SendToClients(std::string message)
{
    std::lock_guard<std::mutex> guard(m_clientSocketsMutex);
    auto client = m_clientsByNickname.begin();
    while (client != m_clientsByNickname.end())
    {
        if (client->second.socket == INVALID_SOCKET)
        {
            client->second.thread.join();
            client = m_clientsByNickname.erase(client);
        } 
        else
        {
            SendToClient(client->second.socket, message);
            client++;
        }
    }
}

void ChatService::SendToClient(SOCKET clientSocket, std::string message)
{
    std::string buffer = g_magicNumberString + message + g_magicNumberString;
    send(clientSocket, buffer.c_str(), buffer.length(), 0);
}

int ChatService::RecieveMessageFromClient(SOCKET clientSocket, std::string& message)
{
    char buffer[BUFFER_SIZE];
    int iResult = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (iResult <= 0)
    {
        return -1;
    }
    if (iResult <= g_magicNumberString.length() * 2)
    {
        return 0;
    }

    std::string bufferStr(buffer, iResult);
    if (bufferStr.substr(0, 4) != g_magicNumberString ||
        bufferStr.substr(bufferStr.length() - 4, 4) != g_magicNumberString) 
    {
        return 0;
    }

    message = bufferStr.substr(4, bufferStr.length() - 8);
    return iResult;
}