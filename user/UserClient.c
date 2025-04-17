/*++
Имя модуля:

    UserClient.c

Описание:

    Этот файл содержит реализацию основной функции
    части пользовательского приложения MarkReader. Эта функция отвечает за
    фактическое сканирование содержимого файла.

Окружение:

    Пользовательский режим

--*/

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <winioctl.h>
#include <string.h>
#include <crtdbg.h>
#include <assert.h>
#include <fltuser.h>
#include "common.h"
#include "UserClient.h"
#include <dontuse.h>

//
//  Количество потоков по умолчанию и максимальное.
//

#define MARK_READER_DEFAULT_REQUEST_COUNT       5
#define MARK_READER_DEFAULT_THREAD_COUNT        2
#define MARK_READER_MAX_THREAD_COUNT            64

//
//  Контекст передается рабочим потокам
//

typedef struct _MARK_READER_THREAD_CONTEXT {

    HANDLE Port;
    HANDLE Completion;

} MARK_READER_THREAD_CONTEXT, * PMARK_READER_THREAD_CONTEXT;


VOID
Usage(
    VOID
)
/*++

Описание функции

    Использование prints

Аргументы

    Нет

Возвращаемое значение

    Нет

--*/
{

    printf("Connects to the MarkReader filter and scans buffers \n");
    printf("Usage: userclient [requests per thread] [number of threads(1-64)]\n");
}

/*++
* Convert from Unicode to current locale
* Use HeapFree() to delete the out string
* --*/
static char* UnicodeToLocal(const WCHAR* Source, ULONG SourceLength)
{
    int Required = WideCharToMultiByte(CP_THREAD_ACP, 0, Source, SourceLength / sizeof(WCHAR), NULL, 0, NULL, NULL);
    Required += 1; // for \0
    char* Buffer = HeapAlloc(GetProcessHeap(), 0, Required);
    if (!Buffer)
        return NULL;

    int Length = WideCharToMultiByte(CP_THREAD_ACP, 0, Source, SourceLength / sizeof(WCHAR), Buffer, Required, NULL, NULL);
    Buffer[Length] = '\0';

    return Buffer;
}


DWORD
MarkReaderWorker(
    _In_ PMARK_READER_THREAD_CONTEXT Context
)
/*++

Описание функции

    Это рабочий поток


Аргументы

    Context  - Этот контекст потока имеет указатель на дескриптор порта, который мы используем для отправки/получения сообщений, и дескриптор порта завершения,
    который уже был связан с портом связи вызывающей стороной.

Возвращаемое значение

    HRESULT указывает на статус выхода потока.

--*/
{
    PMARK_READER_NOTIFICATION notification;
    MARK_READER_REPLY_MESSAGE replyMessage;
    PMARK_READER_MESSAGE message;
    LPOVERLAPPED pOvlp;
    BOOL result;
    DWORD outSize;
    HRESULT hr = E_FAIL;
    ULONG_PTR key;

#pragma warning(push)
#pragma warning(disable:4127) // условное выражение является константой

    while (TRUE) {

#pragma warning(pop)

        //
        //  Опрос сообщений от компонента фильтра для сканирования.
        //

        result = GetQueuedCompletionStatus(Context->Completion, &outSize, &key, &pOvlp, INFINITE);

        //
        //  Получите сообщение: обратите внимание, что сообщение, которое мы отправили через
        //  FltGetMessage(), может НЕ быть тем, которое было выведено из очереди завершения:
        //  это происходит исключительно потому, что на один порт приходится несколько потоков.
        //  Любое из выданных FilterGetMessage() сообщений может быть завершено в случайном порядке —
        //  и мы просто выведем из очереди случайное сообщение.
        //

        message = CONTAINING_RECORD(pOvlp, MARK_READER_MESSAGE, Ovlp);

        if (!result) {

            //
            //  Произошла ошибка.
            //

            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }

        notification = &message->Notification;
        
        char* FileName = notification->FileName.Length ? UnicodeToLocal(notification->FileName.Buffer, notification->FileName.Length) : NULL;
        
        if (notification->Type == MARK_EVENT_FILE_ACCESS) {

            if (notification->FileAccessInfo.Size > MARK_READER_READ_BUFFER_SIZE) {
                printf("ERROR: received message size exceeds %d limit (incompatible driver version?)\n", MARK_READER_READ_BUFFER_SIZE);
                break;
            }
            
            if (notification->FileAccessInfo.Size < 2) {
                // there's definitely no "NO"
                replyMessage.Reply.Rights = 1;
            }
            else {
                _Analysis_assume_(notification->Size <= MARK_READER_READ_BUFFER_SIZE);

                if ((notification->FileAccessInfo.Contents[0] == 'N') && notification->FileAccessInfo.Contents[1] == 'O')
                    replyMessage.Reply.Rights = 0;
                else
                    replyMessage.Reply.Rights = 1;
            }

            printf("Access %s to [%s]\n", (replyMessage.Reply.Rights ? "PERMITTED" : "DENIED"), (FileName ? FileName : "?"));
        } 
        else if (notification->Type == MARK_EVENT_FILE_DELETE) {
            printf("DELETED file [%s]\n", (FileName ? FileName : "?"));
        }
        else {
            printf("ERROR: unknown message type %d (incompatible driver version?)\n", notification->Type);
        }

        if (FileName)
            HeapFree(GetProcessHeap(), 0, FileName);

        // post reply in any case
        replyMessage.ReplyHeader.Status = 0;
        replyMessage.ReplyHeader.MessageId = message->MessageHeader.MessageId;

        hr = FilterReplyMessage(Context->Port,
            (PFILTER_REPLY_HEADER)&replyMessage,
            sizeof(replyMessage));

        if (!SUCCEEDED(hr)) {
            printf("MarkReader: Error replying message. Error = 0x%X\n", hr);
            break;
        }

        memset(&message->Ovlp, 0, sizeof(OVERLAPPED));

        hr = FilterGetMessage(Context->Port,
            &message->MessageHeader,
            FIELD_OFFSET(MARK_READER_MESSAGE, Ovlp),
            &message->Ovlp);

        if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {

            break;
        }
    }

    if (!SUCCEEDED(hr)) {

        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)) {

            //
            //  Порт Mark Reader отключен.
            //

            printf("MarkReader: Port is disconnected, probably due to MarkReader filter unloading.\n");

        }
        else {

            printf("MarkReader: Unknown error occured. Error = 0x%X\n", hr);
        }
    }

    free(message);

    return hr;
}


int _cdecl
main(
    _In_ int argc,
    _In_reads_(argc) char* argv[]
)
{
    DWORD requestCount = MARK_READER_DEFAULT_REQUEST_COUNT;
    DWORD threadCount = MARK_READER_DEFAULT_THREAD_COUNT;
    HANDLE threads[MARK_READER_MAX_THREAD_COUNT];
    MARK_READER_THREAD_CONTEXT context;
    HANDLE port, completion;
    PMARK_READER_MESSAGE msg;
    DWORD threadId;
    HRESULT hr;
    DWORD i, j;

    //
    //  Проверьте желаемое количество потоков и запросов на поток.
    //

    if (argc > 1) {

        requestCount = atoi(argv[1]);

        if (requestCount <= 0) {

            Usage();
            return 1;
        }

        if (argc > 2) {

            threadCount = atoi(argv[2]);
        }

        if (threadCount <= 0 || threadCount > 64) {

            Usage();
            return 1;
        }
    }

    //
    //  Откройте канал связи с фильтром
    //

    printf("MarkReader: Connecting to the filter ...\n");

    hr = FilterConnectCommunicationPort(MarkReaderPortName,
        0,
        NULL,
        0,
        NULL,
        &port);

    if (IS_ERROR(hr)) {

        printf("ERROR: Connecting to filter port: 0x%08x\n", hr);
        return 2;
    }

    //
    //  Создайте порт завершения для связи с этим дескриптором.
    //

    completion = CreateIoCompletionPort(port,
        NULL,
        0,
        threadCount);

    if (completion == NULL) {

        printf("ERROR: Creating completion port: %d\n", GetLastError());
        CloseHandle(port);
        return 3;
    }

    printf("MarkReader: Port = 0x%p Completion = 0x%p\n", port, completion);

    context.Port = port;
    context.Completion = completion;

    //
    //  Создать указанное количество потоков.
    //

    for (i = 0; i < threadCount; i++) {

        threads[i] = CreateThread(NULL,
            0,
            (LPTHREAD_START_ROUTINE)MarkReaderWorker,
            &context,
            0,
            &threadId);

        if (threads[i] == NULL) {

            //
            //  Не удалось создать поток
            //

            hr = GetLastError();
            printf("ERROR: Couldn't create thread: %d\n", hr);
            goto main_cleanup;
        }

        for (j = 0; j < requestCount; j++) {

            //
            //  Определите сообщение
            //

#pragma prefast(suppress:__WARNING_MEMORY_LEAK, "msg will not be leaked because it is freed in MarkReaderWorker")
            msg = malloc(sizeof(MARK_READER_MESSAGE));

            if (msg == NULL) {

                hr = ERROR_NOT_ENOUGH_MEMORY;
                goto main_cleanup;
            }

            memset(&msg->Ovlp, 0, sizeof(OVERLAPPED));

            //
            //  Запрос сообщений от драйвера фильтра.
            //

            hr = FilterGetMessage(port,
                &msg->MessageHeader,
                FIELD_OFFSET(MARK_READER_MESSAGE, Ovlp),
                &msg->Ovlp);

            if (hr != HRESULT_FROM_WIN32(ERROR_IO_PENDING)) {

                free(msg);
                goto main_cleanup;
            }
        }
    }

    hr = S_OK;

    WaitForMultipleObjectsEx(i, threads, TRUE, INFINITE, FALSE);

main_cleanup:

    printf("MarkReader:  All done. Result = 0x%08x\n", hr);

    CloseHandle(port);
    CloseHandle(completion);

    return hr;
}

