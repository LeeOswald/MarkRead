
## Тестирование

Был изменён проект Test. Теперь test.exe имеет следующие варианты использования:

test.exe -h                                             показать подсказку;
test.exe C:\tmp                                         открывать файлы в папкке в бесконечном цикле (т.е., то, что делал старый вариант утилиты);

# мониторинг удаления файлов
test.exe C:\tmp\aaa.txt DeleteFileA                     удалить файл с помощью DeleteFileA;
test.exe C:\tmp\aaa.txt DELETE_ON_CLOSE                 удалить файл путём открытия с флагом DELETE_ON_CLOSE;
test.exe C:\tmp\aaa.txt NtSetInformationFile            удалить файл с помощью NtSetInformationFile(FileDispositionInfo);
test.exe C:\tmp\aaa.txt DeleteFileTransactedA           удалить файл с помощью DeleteFileTransactedA;

Отслеживается только удаление файлов с расширениями, указанными в .inf, удаление папок не отслеживается.
**Поскольку запрет доступа к файлам изначально реализован через FltCancelFileOpen, он не может помешать удалению через DELETE_ON_CLOSE.**

