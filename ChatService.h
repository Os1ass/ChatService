#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <vector>

class ChatService
{
public:
    BOOL Init();

    ChatService(ChatService &other) = delete;
    void operator=(const ChatService &) = delete;

    static ChatService* GetInstance();

    ~ChatService();

private:
    ChatService();


    static DWORD WINAPI WorkerThread(LPVOID lpParam);
    void WorkerThreadImpl();
    void ProcessClient(SOCKET clientSocket);

    static ChatService* s_service;
    HANDLE m_workerThread;
};
