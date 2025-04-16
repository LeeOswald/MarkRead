#pragma once
/*++

Имя модуля:

    UserClient.h

Описание:

    Заголовочный файл, содержащий структуры, определения типов,
    константы, глобальные переменные и прототипы функций для
    части пользовательского режима MarkReader.

Окружение:

    Пользовательский режим

--*/

#pragma pack(1)

typedef struct _MARK_READER_MESSAGE {

    //
    //  Обязательный заголовок структуры.
    //

    FILTER_MESSAGE_HEADER MessageHeader;


    //
    //  private поля, специфичные для MarkReader.
    //

    MARK_READER_NOTIFICATION Notification;

    //
    //  Overlapped структура: это на самом деле не часть сообщения. 
    //  Однако мы встраиваем ее вместо использования отдельно выделенной перекрывающейся структуры.    
    // 

    OVERLAPPED Ovlp;

} MARK_READER_MESSAGE, * PMARK_READER_MESSAGE;

typedef struct _MARK_READER_REPLY_MESSAGE {

    //
    //  Обязательный заголовок структуры.
    //

    FILTER_REPLY_HEADER ReplyHeader;

    //
    //  private поля, специфичные для MarkReader.
    //

    MARK_READER_REPLY Reply;

} MARK_READER_REPLY_MESSAGE, * PMARK_READER_REPLY_MESSAGE;



