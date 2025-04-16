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


#define MARK_READER_READ_BUFFER_SIZE   1024

typedef struct _MARK_READER_NOTIFICATION {

    ULONG Size;
    ULONG Reserved;             // для выравнивания структуры содержимого по четырем словам
    UCHAR Contents[MARK_READER_READ_BUFFER_SIZE];
    
} MARK_READER_NOTIFICATION, *PMARK_READER_NOTIFICATION;

typedef struct _MARK_READER_REPLY {

    UCHAR Rights;
    
} MARK_READER_REPLY, *PMARK_READER_REPLY;



