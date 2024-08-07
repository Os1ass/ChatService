#include "ChatService.h"
#include "tinyxml2.h"
#include <iostream>

#define SERVICE_NAME TEXT("ChatService")

SERVICE_STATUS        g_serviceStatus = { 0 };
SERVICE_STATUS_HANDLE g_serviceStatusHandle = NULL;
HANDLE                g_serviceStopEvent = INVALID_HANDLE_VALUE;
HANDLE                g_serverThread = INVALID_HANDLE_VALUE;
HANDLE                g_pipeThread = INVALID_HANDLE_VALUE;
const LPCWSTR         g_pipeName = L"\\\\.\\pipe\\ServerStatusPipe";
std::string           g_configFileName = "Config.xml";

std::string GetClientsStr(std::string* clients, int clientsSize)
{
    if (clientsSize == 0)
    {
        return g_magicNumberString + g_magicNumberString + '\0';
    }

    std::string clientsStr;
    clientsStr = g_magicNumberString;
    for (int i = 0; i < clientsSize; i++)
    {
        clientsStr += clients[i] + g_magicNumberString;
    }
    delete[] clients;
    return clientsStr + '\0';
}

void ProcessPipeConnection(HANDLE hPipe, LPOVERLAPPED overlap)
{
    std::string* clients = nullptr;
    size_t clientsSize;
    BOOL fSuccess;
    
    SetEvent(overlap->hEvent);
    while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0)
    {
        clientsSize = ChatService::GetInstance()->GetClients(clients);
        std::string clientListStr = GetClientsStr(clients, clientsSize);
        
        ResetEvent(overlap->hEvent);
        fSuccess = WriteFile(
            hPipe,
            clientListStr.c_str(),
            clientListStr.length(),
            NULL,
            overlap
        );

        if (!fSuccess && GetLastError() != ERROR_IO_PENDING)
        {
            OutputDebugString(L"WriteFile failed");
            break;
        }

        while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0 &&
            WaitForSingleObject(overlap->hEvent, 0) != WAIT_OBJECT_0)
        {
            Sleep(500);
        }
        if (WaitForSingleObject(g_serviceStopEvent, 0) == WAIT_OBJECT_0)
        {
            break;
        }
        Sleep(1000);
    }

    DisconnectNamedPipe(hPipe);
}

DWORD WINAPI PipeHandle(LPVOID lpParam)
{
    OVERLAPPED overlap;
    overlap.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlap.Offset = 0;
    overlap.OffsetHigh = 0;

    if (overlap.hEvent == INVALID_HANDLE_VALUE)
    {
        OutputDebugString(L"Unable to create event");
        return GetLastError();
    }

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
        CloseHandle(overlap.hEvent);
        return GetLastError();
    }

    bool fConnected;
    while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0)
    {
        ResetEvent(overlap.hEvent);
        fConnected = ConnectNamedPipe(hPipe, &overlap);
        if (!fConnected && 
            GetLastError() != ERROR_IO_PENDING &&
            GetLastError() != ERROR_PIPE_CONNECTED)
        {
            OutputDebugString(L"ConnectNamedPipe failed");
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            return GetLastError();
        }

        if (GetLastError() != ERROR_PIPE_CONNECTED)
        {
            while (WaitForSingleObject(g_serviceStopEvent, 0) != WAIT_OBJECT_0 &&
                WaitForSingleObject(overlap.hEvent, 0) != WAIT_OBJECT_0)
            {
                Sleep(500);
            }
        }
        if (WaitForSingleObject(g_serviceStopEvent, 0) == WAIT_OBJECT_0)
        {
            break;
        }

        ProcessPipeConnection(hPipe, &overlap);
    }
    CloseHandle(overlap.hEvent);
    CloseHandle(hPipe);
    return 0;
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
        TerminateThread(g_pipeThread, 1);
    }

    CloseHandle(g_pipeThread);
    ChatService::GetInstance()->Stop();
    CloseHandle(g_serverThread);
    ServiceStop();
}

void ParseXmlElement(tinyxml2::XMLElement* chat_server, const char* elementName, std::string& value)
{
    tinyxml2::XMLElement* element = chat_server->FirstChildElement(elementName);
    if (element == 0)
    {
        std::string debugString(elementName);
        debugString = "Not found element with name " + debugString;
        OutputDebugStringA(debugString.c_str());
        return;
    }

    tinyxml2::XMLText* textNode = element->FirstChild()->ToText();
    if (textNode == 0)
    {
        std::string debugString(elementName);
        debugString = "Failed to parse " + debugString;
        OutputDebugStringA(debugString.c_str());
        return;
    }
    value = textNode->Value();
}

void ParseXmlFile()
{
    wchar_t wexecutableFileName[BUFFER_SIZE];
    char executableFileName[BUFFER_SIZE];
    GetModuleFileName(NULL, wexecutableFileName, BUFFER_SIZE);

    size_t fileNameSize;
    wcstombs_s(&fileNameSize, executableFileName, BUFFER_SIZE, wexecutableFileName, _TRUNCATE);
    std::string executableFileNameStr(executableFileName, fileNameSize - strlen("x64/Debug/ChatService.exe") - 1);

    executableFileNameStr = executableFileNameStr + "Config.xml";
    g_configFileName = executableFileNameStr.c_str();

    tinyxml2::XMLDocument config;
    if (config.LoadFile(g_configFileName.c_str()) != tinyxml2::XML_SUCCESS)
    {
        const char* error = tinyxml2::XMLDocument::ErrorIDToName(config.ErrorID());
        OutputDebugStringA(error);
        return;
    }
    
    tinyxml2::XMLElement* chatServer = config.FirstChildElement("chat_server");
    if (chatServer == 0)
    {
        OutputDebugString(L"Element chat_server not found");
        return;
    }

    ParseXmlElement(chatServer, "port", g_serverPort);
    ParseXmlElement(chatServer, "greeting_string", g_greetingString);
}

int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    setlocale(LC_ALL, "RUSSIAN");

    ParseXmlFile();

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