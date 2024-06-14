#include "ChatService.h"

int main()
{
    ChatService service;
    if (!service.Run()) {
        return -1;
    }

    return 0;
}