#pragma once

/*++

Имя модуля:

    common.h

Описание:

    Заголовочный файл, содержащий структуры, определения типов,
    константы, глобальные переменные и прототипы функций, которые
    совместно используются ядром и пользовательским режимом.

Окружение:

    Ядро и пользовательский режим

--*/

//
//  Имя порта, используемого для связи
//

const PWSTR MarkReaderPortName = L"\\MarkReaderPort";

typedef enum {
    MARK_EVENT_FILE_ACCESS = 1,
    MARK_EVENT_FILE_DELETE = 2
} MARK_EVENT_TYPE;


#define MARK_READER_MAX_FILENAME_LENGTH 256 // for demonstration purposes only; real NTFS file paths can be up to 32767 characters long
#define MARK_READER_READ_BUFFER_SIZE   1024

typedef struct _MARK_READER_NOTIFICATION {

    MARK_EVENT_TYPE Type;
    
    struct {
        ULONG Length;                                  // bytes (like in UNICODE_STRING)
        WCHAR Buffer[MARK_READER_MAX_FILENAME_LENGTH]; // no '\0'
    } FileName;

    struct {
        ULONG Size;
        DECLSPEC_ALIGN(16) UCHAR Contents[MARK_READER_READ_BUFFER_SIZE];
    } FileAccessInfo;
    
} MARK_READER_NOTIFICATION, *PMARK_READER_NOTIFICATION;

typedef struct _MARK_READER_REPLY {

    UCHAR Rights;
    
} MARK_READER_REPLY, *PMARK_READER_REPLY;



