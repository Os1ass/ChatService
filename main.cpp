#include "ChatService.h"

#define SERVICE_NAME TEXT("ChatService")

SERVICE_STATUS        g_serviceStatus = { 0 };
SERVICE_STATUS_HANDLE g_serviceStatusHandle = NULL;
HANDLE                g_serviceStopEvent = INVALID_HANDLE_VALUE;
HANDLE                g_serverThread = INVALID_HANDLE_VALUE;

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
    WaitForSingleObject(g_serviceStopEvent, INFINITE);
    ChatService::GetInstance()->Stop();

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