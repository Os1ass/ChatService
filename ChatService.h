#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>

#define DEFAULT_PORT  "26999"
#define BUFFER_SIZE   4096

class ChatService
{
public:
    static DWORD WINAPI StaticRun(LPVOID lpParam);
    void Run();
    void Stop();

    ChatService(ChatService &other) = delete;
    void operator=(const ChatService &) = delete;

    static ChatService* GetInstance();
    size_t GetClients(std::string*& clients);

    ~ChatService();

private:
    ChatService();
    BOOL Init();

    void ProcessClient(std::string clientNickname);
    void SendToClients(std::string message);
    void SendMessageToClient(SOCKET clientSocket, std::string message);
    int RecieveMessageFromClient(SOCKET clientSocket, std::string& message);

    static ChatService* s_service;
    HANDLE m_workerThread;
    std::map<std::string, SOCKET> m_clientSocketsByNickname;
    std::atomic<BOOL> m_cancellationToken;
    std::mutex m_clientSocketsMutex;
    std::vector<std::thread> m_clientThreads;
};
