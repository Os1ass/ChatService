# Windows Service сервер для клиент-серверного приложения чата
## Как пользоваться
- Склонировать репозиторий
- Собрать проект
- Открыть командную строку от имени администратора
- Ввести команды
```
sc create ChatService binPath= <абсолютный путь до ChatService.exe>
sc start ChatService
```
- Чтобы проверить статус сервиса нужно ввести `sc qc ChatService`
- Чтобы остановить сервис нужно ввести `sc stop ChatService`
- Чтобы удалить сервис нужно ввести `sc delete ChatService`

Конфиргурация сервера происходит с помощью файла config.xml в корневой папке. Для парсинга xml файла используется [tinyxml2](https://github.com/leethomason/tinyxml2).
