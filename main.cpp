#include "ChatService.h"

#define SERVICE_NAME TEXT("ChatService")

SERVICE_STATUS        g_serviceStatus = { 0 };
SERVICE_STATUS_HANDLE g_serviceStatusHandle = NULL;
HANDLE                g_serviceStopEvent = INVALID_HANDLE_VALUE;
HANDLE                g_serverThread = INVALID_HANDLE_VALUE;
HANDLE                g_pipeThread = INVALID_HANDLE_VALUE;
const LPCWSTR         g_pipeName = L"\\.\pipe\ServerStatusPipe";
const BYTE            g_pipeSeparator = 0xEE;
const std::string     g_pipeSeparatorStr(reinterpret_cast<const char*>(g_pipeSeparator), sizeof(g_pipeSeparator));

void ProcessPipeConnection(HANDLE hPipe, OVERLAPPED overlap)
{
    std::string* clients;
    size_t clientsSize;
    BOOL fSuccess;

    while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0)
    {
        if (WaitForSingleObject(overlap.hEvent, 0) != WAIT_OBJECT_0)
        {
            Sleep(1000);
            continue;
        }

        clientsSize = ChatService::GetInstance()->GetClients(clients);
        if (clients == nullptr || clientsSize == 0)
        {
            Sleep(1000);
            continue;
        }

        std::string clientListStr;
        for (int i = 0; i < clientsSize - 1; i++)
        {
            clientListStr += clients[i] + g_pipeSeparatorStr;
        }
        clientListStr += clients[clientsSize - 1];
        delete[] clients;

        fSuccess = WriteFile(
            hPipe,
            clientListStr.c_str(),
            clientListStr.length(),
            NULL,
            &overlap
        );

        if (fSuccess && GetLastError() != ERROR_IO_PENDING)
        {
            OutputDebugString(L"WriteFile failed");
            break;
        }

        Sleep(1000);
    }

    DisconnectNamedPipe(hPipe);
}

static DWORD WINAPI PipeHandle(LPVOID lpParam)
{
    HANDLE hPipe = CreateNamedPipe(
        g_pipeName,
        PIPE_ACCESS_OUTBOUND |
        FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | 
        PIPE_READMODE_BYTE | 
        PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        BUFFER_SIZE,
        BUFFER_SIZE,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        OutputDebugString(L"Unable to created named pipe");
        return GetLastError();
    }

    OVERLAPPED overlap;
    overlap.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlap.Offset = 0;
    overlap.OffsetHigh = 0;

    if (overlap.hEvent == INVALID_HANDLE_VALUE)
    {
        OutputDebugString(L"Unable to create event");
        CloseHandle(hPipe);
        return GetLastError();
    }

    bool fConnected = ConnectNamedPipe(hPipe, &overlap);
    if (fConnected)
    {
        OutputDebugString(L"ConnectNamedPipe failed");
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
        return GetLastError();
    }

    while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0)
    {
        if (WaitForSingleObject(overlap.hEvent, 0) != WAIT_OBJECT_0)
        {
            Sleep(500);
            continue;
        }

        ProcessPipeConnection(hPipe, overlap);
    }

    CloseHandle(hPipe);
}

VOID ServiceStop()
{
    if (g_serviceStopEvent == INVALID_HANDLE_VALUE ||
        g_serviceStatus.dwCurrentState != SERVICE_RUNNING)
        return;

    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    g_serviceStatus.dwControlsAccepted = 0;
    g_serviceStatus.dwWin32ExitCode = 0;

    if (SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus) == FALSE)
    {
        OutputDebugString(TEXT("Unable to set service status (ServiceStop)"));
        return;
    }

    SetEvent(g_serviceStopEvent);
    return;
}

VOID ServiceStop(DWORD lastError)
{
    if (g_serviceStopEvent == INVALID_HANDLE_VALUE ||
        g_serviceStatus.dwCurrentState != SERVICE_RUNNING)
        return;

    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    g_serviceStatus.dwControlsAccepted = 0;
    g_serviceStatus.dwWin32ExitCode = lastError;

    if (SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus) == FALSE)
    {
        OutputDebugString(TEXT("Unable to set service status (ServiceStop)"));
        return;
    }

    SetEvent(g_serviceStopEvent);
    return;
}

VOID WINAPI ServiceCtrlHandler(DWORD dwCode)
{
    switch (dwCode)
    {
    case SERVICE_CONTROL_STOP:
        ServiceStop();
        break;
    default:
        break;
    }
}

BOOL ServiceInit()
{
    g_serviceStatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_serviceStatusHandle)
    {
        OutputDebugString(TEXT("Unable to register service control handler"));
        return FALSE;
    }

    SecureZeroMemory(&g_serviceStatus, sizeof(g_serviceStatus));
    g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    if (SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus) == FALSE)
    {
        OutputDebugString(TEXT("Unable to set service status (ServiceInit)"));
        return FALSE;
    }

    g_serviceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_serviceStopEvent == NULL)
    {
        ServiceStop(GetLastError());
        return FALSE;
    }

    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    g_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    if (SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus) == FALSE)
    {
        OutputDebugString(TEXT("Unable to set service status (ServiceInit)"));
        return FALSE;
    }

    return TRUE;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    if (!ServiceInit())
    {
        OutputDebugString(TEXT("Unable to init service"));
        return;
    }
    
    g_serverThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ChatService::StaticRun, ChatService::GetInstance(), 0, NULL);
    if (g_serverThread == NULL)
    {
        OutputDebugString(TEXT("Unable to create thread"));
        ServiceStop();
    }

    g_pipeThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PipeHandle, NULL, 0, NULL);
    if (g_pipeThread == NULL)
    {
        ChatService::GetInstance()->Stop();
        CloseHandle(g_serverThread);
        OutputDebugString(TEXT("Unable to create thread"));
        ServiceStop();
    }

    WaitForSingleObject(g_serviceStopEvent, INFINITE);
    if (WaitForSingleObject(g_pipeThread, 5000) != WAIT_OBJECT_0)
    {
        TerminateThread(g_pipeThread, 0);
    }

    CloseHandle(g_pipeThread);
    ChatService::GetInstance()->Stop();
    CloseHandle(g_serverThread);
    ServiceStop();
}

int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    SERVICE_TABLE_ENTRY serviceTable[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain},
        { NULL, NULL }
    };

    if (StartServiceCtrlDispatcher(serviceTable) == FALSE)
    {
        return GetLastError();
    }

    return 0;
}