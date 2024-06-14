#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <vector>

class ChatService
{
public:
    ChatService();
    virtual ~ChatService();

    BOOL Run();
    void Stop();

protected:
    BOOL Init();
    void ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv);
    static void WINAPI ServiceMainWrapper(DWORD dwArgc, LPTSTR* lpszArgv);
    static void WINAPI ServiceCtrlHandler(DWORD dwCtrl);

    static DWORD WINAPI WorkerThread(LPVOID lpParam);
    void WorkerThreadImpl();
    void ProcessClient(SOCKET clientSocket);

private:
    static ChatService* s_service;
    SERVICE_STATUS m_serviceStatus;
    SERVICE_STATUS_HANDLE m_serviceStatusHandle;
    HANDLE m_stopEvent;
    HANDLE m_workerThread;
};
