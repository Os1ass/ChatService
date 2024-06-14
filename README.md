# Windows Service сервер для клиен-серверного приложения чата
## Как пользоваться
- Склонировать репозиторий
- Собрать проект
- Открыть командную строку от имени администратора
- Ввести команды
- 'sc create ChatService binPath= <абсолютный путь до ChatService.exe>'
- 'sc start ChatService'
- Для проверки состояния сервиса
- 'sc qc ChatService'
- Чтобы остановить сервис надо ввести
- 'sc stop ChatService'
- Чтобы удалить сервис надо ввести
- 'sc delete ChatService'
