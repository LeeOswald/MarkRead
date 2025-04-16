
#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include "common.h"
#include "MarkReader.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

#define MARK_READER_REG_TAG       'Rncs'
#define MARK_READER_STRING_TAG    'Sncs'

//
//  Структура, содержащая все глобальные структуры данных, используемые во всем MarkReader
//

MARK_READER_DATA MarkReaderData;

//
//  Это статический список файлов с расширениями имен, которые мы отслеживаем
//

PUNICODE_STRING ScannedExtensions;
ULONG ScannedExtensionCount;

//
//  Расширение по умолчанию для сканирования, если оно не настроено в реестре
//

UNICODE_STRING ScannedExtensionDefault = RTL_CONSTANT_STRING( L"doc" );

//
// Игнорируем пид дружественного процесса чтения
//

UINT64 IgnoreIRP_Pid = 0;

//
//  Прототипы функций
//

typedef
NTSTATUS
(*PFN_IoOpenDriverRegistryKey) (
    PDRIVER_OBJECT     DriverObject,
    DRIVER_REGKEY_TYPE RegKeyType,
    ACCESS_MASK        DesiredAccess,
    ULONG              Flags,
    PHANDLE            DriverRegKey
    );

PFN_IoOpenDriverRegistryKey
MarkReaderGetIoOpenDriverRegistryKey (
    VOID
    );

NTSTATUS
MarkReaderOpenServiceParametersKey (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING ServiceRegistryPath,
    _Out_ PHANDLE ServiceParametersKey
    );

NTSTATUS
MarkReaderInitializeIgnoreIRP_Pid(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
MarkReaderInitializeScannedExtensions(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

VOID
MarkReaderFreeExtensions(
    );

NTSTATUS
MarkReaderAllocateUnicodeString (
    _Inout_ PUNICODE_STRING String
    );

VOID
MarkReaderFreeUnicodeString (
    _Inout_ PUNICODE_STRING String
    );

NTSTATUS
MarkReaderPortConnect (
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie
    );

VOID
MarkReaderPortDisconnect (
    _In_opt_ PVOID ConnectionCookie
    );

NTSTATUS
MarkReaderpScanFileInUserMode (
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PBOOLEAN Rights
    );

BOOLEAN
MarkReaderCheckExtension (
    _In_ PUNICODE_STRING Extension
    );

//
//  Назначим текстовые разделы для каждой процедуры
//

#ifdef ALLOC_PRAGMA
    #pragma alloc_text(INIT, DriverEntry)
    #pragma alloc_text(INIT, MarkReaderGetIoOpenDriverRegistryKey)
    #pragma alloc_text(INIT, MarkReaderOpenServiceParametersKey)
    #pragma alloc_text(INIT, MarkReaderInitializeScannedExtensions)
    #pragma alloc_text(INIT, MarkReaderInitializeIgnoreIRP_Pid)
    #pragma alloc_text(PAGE, MarkReaderInstanceSetup)
    #pragma alloc_text(PAGE, MarkReaderPreCreate)
    #pragma alloc_text(PAGE, MarkReaderPortConnect)
    #pragma alloc_text(PAGE, MarkReaderPortDisconnect)
    #pragma alloc_text(PAGE, MarkReaderFreeExtensions)
    #pragma alloc_text(PAGE, MarkReaderAllocateUnicodeString)
    #pragma alloc_text(PAGE, MarkReaderFreeUnicodeString)
#endif


//
//  Постоянная структура FLT_REGISTRATION для нашего фильтра. Она инициализирует функции 
//  обратного вызова, для которых наш фильтр хочет зарегистрироваться. Это используется 
//  только для регистрации в менеджере фильтров
//

/// Получить pid процесса, который обращается к файлу.
///
/// @param[IN] <data> Указатель на структуру представления IRP пакета операции FLT_CALLBACK_DATA (см. fltkernel.h).
/// @return ID процесса. 0 - если ошибка.
UINT64 GetIOProcessId(PFLT_CALLBACK_DATA data)
{
    PEPROCESS pProc = IoThreadToProcess(data->Thread);
    if (!pProc)
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MarkReader Error: GetIOProcessId. Can not get proccess\n");
        return 0;
    }

    HANDLE pid = PsGetProcessId(pProc);
    if (pid == NULL)
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "MarkReader Error: GetIOProcessId. Can not get proccess id\n");
        return 0;
    }

    return (UINT64)pid;
}

const FLT_OPERATION_REGISTRATION Callbacks[] = {

    { IRP_MJ_CREATE,
      0,
      MarkReaderPreCreate,
      MarkReaderPostCreate},

    { IRP_MJ_CLEANUP,
      0,
      MarkReaderPreCleanup,
      NULL},

    { IRP_MJ_WRITE,
      0,
      MarkReaderPreWrite,
      NULL},

#if (WINVER>=0x0602)

    { IRP_MJ_FILE_SYSTEM_CONTROL,
      0,
      MarkReaderPreFileSystemControl,
      NULL
    },

#endif

    { IRP_MJ_OPERATION_END}
};


const FLT_CONTEXT_REGISTRATION ContextRegistration[] = {

    { FLT_STREAMHANDLE_CONTEXT,
      0,
      NULL,
      sizeof(MARK_READER_STREAM_HANDLE_CONTEXT),
      'chBS' },

    { FLT_CONTEXT_END }
};

const FLT_REGISTRATION FilterRegistration = {

    sizeof( FLT_REGISTRATION ),         //  Размер
    FLT_REGISTRATION_VERSION,           //  Версия
    0,                                  //  Фдаги
    ContextRegistration,                //  Регистрация контекста
    Callbacks,                          //  Обратные выховы
    MarkReaderUnload,                      //  FilterUnload
    MarkReaderInstanceSetup,               //  InstanceSetup
    MarkReaderQueryTeardown,               //  InstanceQueryTeardown
    NULL,                               //  InstanceTeardownStart
    NULL,                               //  InstanceTeardownComplete
    NULL,                               //  GenerateFileName
    NULL,                               //  GenerateDestinationFileName
    NULL                                //  NormalizeNameComponent
};

////////////////////////////////////////////////////////////////////////////
//
//    Функции инициализации и выгрузки фильтра
//
////////////////////////////////////////////////////////////////////////////

NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Описание функции:

    Это функция инициализации для драйвера фильтра. Она
    регистрирует фильтр в диспетчере фильтров и инициализирует все
    его глобальные структуры данных.

Аргументы:

    DriverObject — указатель на объект драйвера, созданный системой для
    представления этого драйвера.

    RegistryPath — строка Unicode, указывающая, где в реестре находятся параметры для этого
    драйвера.

Возвращаемое значение:

    Возвращает STATUS_SUCCESS.
--*/
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING uniString;
    PSECURITY_DESCRIPTOR sd;
    NTSTATUS status;

    //
    //  По умолчанию используется NonPagedPoolNx для выделений невыгружаемого пула, если это поддерживается.
    //

    ExInitializeDriverRuntime( DrvRtPoolNxOptIn );

    //
    //  Зарегистрируйемся с помощью менеджера фильтров.
    //

    status = FltRegisterFilter( DriverObject,
                                &FilterRegistration,
                                &MarkReaderData.Filter );


    if (!NT_SUCCESS( status )) {

        return status;
    }

    //
    // Получим расширения для мониторинга из реестра
    //

    status = MarkReaderInitializeScannedExtensions( DriverObject, RegistryPath );

    if (!NT_SUCCESS( status )) {

        status = STATUS_SUCCESS;

        ScannedExtensions = &ScannedExtensionDefault;
        ScannedExtensionCount = 1;
    }

    //!!!status = MarkReaderInitializeIgnoreIRP_Pid(DriverObject, RegistryPath);

    //if (!NT_SUCCESS(status)) {

    //    status = STATUS_SUCCESS;

    //}
    //DbgPrint("MarkRead: Ignoring IO from PID:%d\n", (int )IgnoreIRP_Pid);

    //
    //  Создадим порт связи.
    //

    RtlInitUnicodeString( &uniString, MarkReaderPortName );

    //
    //  Мы защищаем порт, поэтому доступ к нему могут получить только АДМИНЫ и СИСТЕМА.
    //

    status = FltBuildDefaultSecurityDescriptor( &sd, FLT_PORT_ALL_ACCESS );

    if (NT_SUCCESS( status )) {

        InitializeObjectAttributes( &oa,
                                    &uniString,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL,
                                    sd );

        status = FltCreateCommunicationPort( MarkReaderData.Filter,
                                             &MarkReaderData.ServerPort,
                                             &oa,
                                             NULL,
                                             MarkReaderPortConnect,
                                             MarkReaderPortDisconnect,
                                             NULL,
                                             1 );
        //
        //  Освободим дескриптор безопасности во всех случаях. Он не нужен после вызова FltCreateCommunicationPort().
        //

        FltFreeSecurityDescriptor( sd );

        if (NT_SUCCESS( status )) {

            //
            //  Начнем фильтрацию I/O.
            //

            status = FltStartFiltering( MarkReaderData.Filter );

            if (NT_SUCCESS( status )) {

                return STATUS_SUCCESS;
            }

            FltCloseCommunicationPort( MarkReaderData.ServerPort );
        }
    }

    MarkReaderFreeExtensions();

    FltUnregisterFilter( MarkReaderData.Filter );

    return status;
}


PFN_IoOpenDriverRegistryKey
MarkReaderGetIoOpenDriverRegistryKey (
    VOID
    )
{
    static PFN_IoOpenDriverRegistryKey pIoOpenDriverRegistryKey = NULL;
    UNICODE_STRING FunctionName = {0};

    if (pIoOpenDriverRegistryKey == NULL) {

        RtlInitUnicodeString(&FunctionName, L"IoOpenDriverRegistryKey");

        pIoOpenDriverRegistryKey = (PFN_IoOpenDriverRegistryKey)MmGetSystemRoutineAddress(&FunctionName);
    }

    return pIoOpenDriverRegistryKey;
}

NTSTATUS
MarkReaderOpenServiceParametersKey (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING ServiceRegistryPath,
    _Out_ PHANDLE ServiceParametersKey
    )
/*++

Описание функции:

    Эта функция открывает ключ параметров службы, используя API, совместимые с изоляцией, когда это возможно.

Аргументы:

    DriverObject — указатель на объект драйвера, созданный системой для представления этого драйвера.
    
    RegistryPath — ключ пути, переданный драйверу во время DriverEntry.

    ServiceParametersKey — возвращает дескриптор подключаемого ключа параметров службы.

Возвращаемое значение:

    STATUS_SUCCESS, если функция завершается успешно. В противном случае возвращается действительный код NTSTATUS.

--*/
{
    NTSTATUS status;
    PFN_IoOpenDriverRegistryKey pIoOpenDriverRegistryKey;
    UNICODE_STRING Subkey;
    HANDLE ParametersKey = NULL;
    HANDLE ServiceRegKey = NULL;
    OBJECT_ATTRIBUTES Attributes;

    //
    //  Откроем ключ параметров, чтобы прочитать значения из INF-файла, используя API для открытия ключа, если это возможно.
    //

    pIoOpenDriverRegistryKey = MarkReaderGetIoOpenDriverRegistryKey();

    if (pIoOpenDriverRegistryKey != NULL) {

        //
        //  Откроем ключ параметров с помощью API
        //

        status = pIoOpenDriverRegistryKey( DriverObject,
                                           DriverRegKeyParameters,
                                           KEY_READ,
                                           0,
                                           &ParametersKey );

        if (!NT_SUCCESS( status )) {

            goto MarkReaderOpenServiceParametersKeyCleanup;
        }

    } else {

        //
        //  Откроем указанный корневой ключ службы
        //

        InitializeObjectAttributes( &Attributes,
                                    ServiceRegistryPath,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL,
                                    NULL );

        status = ZwOpenKey( &ServiceRegKey,
                            KEY_READ,
                            &Attributes );

        if (!NT_SUCCESS( status )) {

            goto MarkReaderOpenServiceParametersKeyCleanup;
        }

        //
        //  Откроем ключ параметров относительно пути ключа службы
        //

        RtlInitUnicodeString( &Subkey, L"Parameters" );

        InitializeObjectAttributes( &Attributes,
                                    &Subkey,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    ServiceRegKey,
                                    NULL );

        status = ZwOpenKey( &ParametersKey,
                            KEY_READ,
                            &Attributes );

        if (!NT_SUCCESS( status )) {

            goto MarkReaderOpenServiceParametersKeyCleanup;
        }
    }

    //
    //  Возвращаемое значение вызывающей функции
    //

    *ServiceParametersKey = ParametersKey;

MarkReaderOpenServiceParametersKeyCleanup:

    if (ServiceRegKey != NULL) {

        ZwClose( ServiceRegKey );
    }

    return status;

}

NTSTATUS
MarkReaderInitializeIgnoreIRP_Pid(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
/*++

Описание функции:

    Эта функция устанавливает IgnoreIRP_PID на основе

    реестра.

Аргументы:

    DriverObject — указатель на объект драйвера, созданный системой для

    представления этого драйвера.

    RegistryPath — ключ пути, переданный драйверу во время DriverEntry.

Возвращаемое значение:

    STATUS_SUCCESS, если функция завершается успешно. В противном случае возвращается допустимый

    код NTSTATUS.

--*/
{
    NTSTATUS status;
    HANDLE driverRegKey = NULL;
    UNICODE_STRING valueName;
    PKEY_VALUE_PARTIAL_INFORMATION valueBuffer = NULL;
    ULONG valueLength = 0;

    PAGED_CODE();

    //
    //  Откроем ключ параметров службы, из которого следует запросить значения.
    //

    status = MarkReaderOpenServiceParametersKey(DriverObject,
        RegistryPath,
        &driverRegKey);

    if (!NT_SUCCESS(status)) {

        driverRegKey = NULL;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    //
    //   Запрос длины значения reg
    //

    RtlInitUnicodeString(&valueName, L"IgnoreIRP_Pid");

    status = ZwQueryValueKey(driverRegKey,
        &valueName,
        KeyValuePartialInformation,
        NULL,
        0,
        &valueLength);

    if (status != STATUS_BUFFER_TOO_SMALL && status != STATUS_BUFFER_OVERFLOW) {

        status = STATUS_INVALID_PARAMETER;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    //
    //  Извлекаем путь.
    //

    valueBuffer = ExAllocatePoolZero(NonPagedPool,
        valueLength,
        MARK_READER_REG_TAG);

    if (valueBuffer == NULL) {

        status = STATUS_INSUFFICIENT_RESOURCES;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    status = ZwQueryValueKey(driverRegKey,
        &valueName,
        KeyValuePartialInformation,
        valueBuffer,
        valueLength,
        &valueLength);

    if (!NT_SUCCESS(status)) {

        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    IgnoreIRP_Pid = (UINT64 )(valueBuffer->Data);

MarkReaderInitializeScannedExtensionsCleanup:
    //  Обратите внимание, что эта функция пропускает глобальные буферы. 
    //  При сбое DriverEntry очистит глобальные буферы, поэтому нам не нужно делать это здесь.
    //

    if (valueBuffer != NULL) {

        ExFreePoolWithTag(valueBuffer, MARK_READER_REG_TAG);
        valueBuffer = NULL;
    }

    if (driverRegKey != NULL) {

        ZwClose(driverRegKey);
    }

    if (!NT_SUCCESS(status)) {

        MarkReaderFreeExtensions();
    }

    return status;
}

NTSTATUS
MarkReaderInitializeScannedExtensions(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Описание функции:

    Эта функция задает расширения для файлов, которые будут сканироваться на основе
    реестра.

Аргументы:

    DriverObject — указатель на объект драйвера, созданный системой для
    представления этого драйвера.

    RegistryPath — ключ пути, переданный драйверу во время DriverEntry.

Возвращаемое значение:

    STATUS_SUCCESS, если функция завершается успешно. В противном случае возвращается допустимый
    код NTSTATUS.

--*/
{
    NTSTATUS status;
    HANDLE driverRegKey = NULL;
    UNICODE_STRING valueName;
    PKEY_VALUE_PARTIAL_INFORMATION valueBuffer = NULL;
    ULONG valueLength = 0;
    PWCHAR ch;
    SIZE_T length;
    ULONG count;
    PUNICODE_STRING ext;

    PAGED_CODE();

    ScannedExtensions = NULL;
    ScannedExtensionCount = 0;

    //
    //  Откроем ключ параметров службы, из которого следует запросить значения.
    //

    status = MarkReaderOpenServiceParametersKey( DriverObject,
                                              RegistryPath,
                                              &driverRegKey );

    if (!NT_SUCCESS( status )) {

        driverRegKey = NULL;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    //
    //   Запрос длины значения reg
    //

    RtlInitUnicodeString( &valueName, L"Extensions" );

    status = ZwQueryValueKey( driverRegKey,
                              &valueName,
                              KeyValuePartialInformation,
                              NULL,
                              0,
                              &valueLength );

    if (status!=STATUS_BUFFER_TOO_SMALL && status!=STATUS_BUFFER_OVERFLOW) {

        status = STATUS_INVALID_PARAMETER;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    //
    //  Извлекаем путь
    //

    valueBuffer = ExAllocatePoolZero( NonPagedPool,
                                      valueLength,
                                      MARK_READER_REG_TAG );

    if (valueBuffer == NULL) {

        status = STATUS_INSUFFICIENT_RESOURCES;
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    status = ZwQueryValueKey( driverRegKey,
                              &valueName,
                              KeyValuePartialInformation,
                              valueBuffer,
                              valueLength,
                              &valueLength );

    if (!NT_SUCCESS( status )) {

        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    ch = (PWCHAR)(valueBuffer->Data);

    count = 0;

    //
    //  Подсчитаем, сколько строк в мультистроке
    //

    while (*ch != '\0') {

        ch = ch + wcslen( ch ) + 1;
        count++;
    }

    ScannedExtensions = ExAllocatePoolZero( PagedPool,
                                            count * sizeof(UNICODE_STRING),
                                            MARK_READER_STRING_TAG );

    if (ScannedExtensions == NULL) {
        goto MarkReaderInitializeScannedExtensionsCleanup;
    }

    ch = (PWCHAR)((PKEY_VALUE_PARTIAL_INFORMATION)valueBuffer->Data);
    ext = ScannedExtensions;

    while (ScannedExtensionCount < count) {

        length = wcslen( ch ) * sizeof(WCHAR);

        ext->MaximumLength = (USHORT) length;

        status = MarkReaderAllocateUnicodeString( ext );

        if (!NT_SUCCESS( status )) {
            goto MarkReaderInitializeScannedExtensionsCleanup;
        }

        ext->Length = (USHORT)length;

        RtlCopyMemory( ext->Buffer, ch, length );

        ch = ch + length/sizeof(WCHAR) + 1;

        ScannedExtensionCount++;

        ext++;

    }

MarkReaderInitializeScannedExtensionsCleanup:

    //
    //  Обратите внимание, что эта функция пропускает глобальные буферы. При сбое DriverEntry очистит глобальные буферы, 
    //  поэтому нам не нужно делать это здесь.
    //

    if (valueBuffer != NULL) {

        ExFreePoolWithTag( valueBuffer, MARK_READER_REG_TAG );
        valueBuffer = NULL;
    }

    if (driverRegKey != NULL) {

        ZwClose( driverRegKey );
    }

    if (!NT_SUCCESS( status )) {

        MarkReaderFreeExtensions();
    }

    return status;
}


VOID
MarkReaderFreeExtensions(
    )
/*++

Описание функции:

    Эта функция очищает глобальные буферы как при сбое, так и при сбое инициализации.

Аргументы:

Возвращаемое значение:

    Нет.

--*/
{
    PAGED_CODE();

    //
    // Освободим строки в сканированном массиве расширения
    //

    while (ScannedExtensionCount > 0) {

        ScannedExtensionCount--;

        if (ScannedExtensions != &ScannedExtensionDefault) {

            MarkReaderFreeUnicodeString( ScannedExtensions + ScannedExtensionCount );
        }
    }

    if (ScannedExtensions != &ScannedExtensionDefault && ScannedExtensions != NULL) {

        ExFreePoolWithTag( ScannedExtensions, MARK_READER_STRING_TAG );
    }

    ScannedExtensions = NULL;

}


NTSTATUS
MarkReaderAllocateUnicodeString (
    _Inout_ PUNICODE_STRING String
    )
/*++

Описание функции:

    Эта функция выделяет строку Unicode

Аргументы:

    String - предоставляет размер строки, которая будет выделена в поле MaximumLength
        возвращает строку unicode

Возвращаемое значение:

    STATUS_SUCCESS                  - успех
    STATUS_INSUFFICIENT_RESOURCES   - неудача

--*/
{

    PAGED_CODE();

    String->Buffer = ExAllocatePoolZero( NonPagedPool,
                                         String->MaximumLength,
                                         MARK_READER_STRING_TAG );

    if (String->Buffer == NULL) {

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    String->Length = 0;

    return STATUS_SUCCESS;
}


VOID
MarkReaderFreeUnicodeString (
    _Inout_ PUNICODE_STRING String
    )
/*++

Описание функции:

    Эта функция освобождает строку Unicode

Аргументы:

    String - строка, которую нужно освободить

Возвращаемое значение:

    Нет

--*/
{
    PAGED_CODE();

    if (String->Buffer) {

        ExFreePoolWithTag( String->Buffer,
                           MARK_READER_STRING_TAG );
        String->Buffer = NULL;
    }

    String->Length = String->MaximumLength = 0;
    String->Buffer = NULL;
}


NTSTATUS
MarkReaderPortConnect (
    _In_ PFLT_PORT ClientPort,
    _In_opt_ PVOID ServerPortCookie,
    _In_reads_bytes_opt_(SizeOfContext) PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Outptr_result_maybenull_ PVOID *ConnectionCookie
    )
/*++

Описание функции

    Вызывается, когда пользовательский режим подключается к порту сервера — для установления соединения

Аргументы

    ClientPort — это порт клиентского подключения, который будет использоваться для
    отправки сообщений из фильтра

    ServerPortCookie — контекст, связанный с этим портом, когда
    минифильтр создал этот порт.

    ConnectionContext — контекст от сущности, подключающейся к этому порту (скорее всего, ваша служба пользовательского режима)

    SizeofContext — размер ConnectionContext в байтах

    ConnectionCookie — контекст, который будет передан в процедуру отключения порта.

Возвращаемое значение

    STATUS_SUCCESS - принять соединение

--*/
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER( ServerPortCookie );
    UNREFERENCED_PARAMETER( ConnectionContext );
    UNREFERENCED_PARAMETER( SizeOfContext);
    UNREFERENCED_PARAMETER( ConnectionCookie = NULL );

    FLT_ASSERT( MarkReaderData.ClientPort == NULL );
    FLT_ASSERT( MarkReaderData.UserProcess == NULL );

    //
    //  Установим пользовательский процесс и порт. В производственном фильтре 
    //  может потребоваться синхронизировать доступ к таким полям с продолжительностью жизни порта. 
    //  Например, в то время как менеджер фильтров синхронизирует FltCloseClientPort 
    //  с чтением дескриптора порта FltSendMessage, синхронизация доступа к UserProcess 
    //  будет зависеть от фильтра.
    //

    MarkReaderData.UserProcess = PsGetCurrentProcess();
    MarkReaderData.ClientPort = ClientPort;

    DbgPrint( "MarkReader is connected. Port=0x%p\n", ClientPort );

    return STATUS_SUCCESS;
}


VOID
MarkReaderPortDisconnect(
     _In_opt_ PVOID ConnectionCookie
     )
/*++

Описание функции

    Вызывается, когда соединение разрывается. Мы используем ее, чтобы закрыть
    дескриптор соединения

Аргументы

    ConnectionCookie - Контекст из процедуры подключения порта

Возвращаемое значение

    Нет

--*/
{
    UNREFERENCED_PARAMETER( ConnectionCookie );

    PAGED_CODE();

    DbgPrint( "MarkReader is disconnected. Port=0x%p\n", MarkReaderData.ClientPort );

    //
    //  Закрываем дескриптор соединения: обратите внимание, поскольку мы ограничили 
    //  максимальное количество соединений до 1, другое соединение не будет разрешено, 
    //  пока мы не вернёмся из процедуры отключения.
    //

    FltCloseClientPort( MarkReaderData.Filter, &MarkReaderData.ClientPort );

    //
    //  Сброс поля пользовательского процесса.
    //

    MarkReaderData.UserProcess = NULL;
}


NTSTATUS
MarkReaderUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    )
/*++

Описание функции:

    Это функция выгрузки для драйвера фильтра. Она отменяет регистрацию фильтра 
    в диспетчере фильтров и освобождает все выделенные глобальные структуры данных.

Аргументы:

    Нет.

Возвращаемое значение:

    Возвращает окончательное состояние функции освобождения.

--*/
{
    UNREFERENCED_PARAMETER( Flags );

    MarkReaderFreeExtensions();

    //
    //  Закроем порт сервера.
    //

    FltCloseCommunicationPort( MarkReaderData.ServerPort );

    //
    //  Отменим регистрацию фильтра.
    //

    FltUnregisterFilter( MarkReaderData.Filter );

    return STATUS_SUCCESS;
}


NTSTATUS
MarkReaderInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    )
/*++

Описание функции:

    Эта функция вызывается диспетчером фильтров при создании нового экземпляра.
    Мы указали в реестре, что нам нужны только ручные вложения,
    так что это все, что мы должны здесь получить.

Аргументы:

    FltObjects — описывает экземпляр и том, который нам предлагается
    настроить.

    Flags — флаги, описывающие тип этого вложения.

    VolumeDeviceType — DEVICE_TYPE для тома, к которому этот экземпляр
    будет присоединен.

    VolumeFileSystemType — файловая система, отформатированная на этом томе.

Возвращаемое значение:

    STATUS_SUCCESS — мы хотим присоединить к тому
    STATUS_FLT_DO_NOT_ATTACH — нет, спасибо

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );
    UNREFERENCED_PARAMETER( VolumeFilesystemType );

    PAGED_CODE();

    FLT_ASSERT( FltObjects->Filter == MarkReaderData.Filter );

    //
    //  Не подключаемся к сетевым томам.
    //

    if (VolumeDeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM) {

       return STATUS_FLT_DO_NOT_ATTACH;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MarkReaderQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    )
/*++

Описание функции:

    Это функция отсоединения экземпляра для фильтра. Она вызывается диспетчером фильтров, 
    когда пользователь инициирует ручное отсоединение экземпляра. Это функция «запроса»: 
    если фильтр не хочет поддерживать ручное отсоединение, он может вернуть статус сбоя

Аргументы:

    FltObjects — описывает экземпляр и том, для которого мы получаем

    этот запрос на отсоединение.

    Flags — не используется

Возвращаемое значение:

    STATUS_SUCCESS — мы разрешаем отсоединение экземпляра

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( Flags );

    return STATUS_SUCCESS;
}


FLT_PREOP_CALLBACK_STATUS
MarkReaderPreCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Описание функции:

    Обратный вызов до создания. Нам нужно запомнить, был ли этот файл
    открыт для записи. Если да, то мы захотим повторно просканировать его при очистке.
    Эта схема приводит к дополнительным сканированиям как минимум в двух случаях:
    - если создание не удалось (возможно, из-за отказа в доступе)
    - файл открыт для записи, но фактически не записан
    Предполагается, что записи встречаются чаще, чем создания, и проверка
    или установка контекста в пути записи будет менее эффективной, чем
    угадывание перед созданием.

Аргументы:

    Data - Структура, описывающая параметры операции.

    FltObject - Структура, описывающая объекты, затронутые этой
    операцией.

    CompletionContext - Выходной параметр, который можно использовать для передачи контекста
    из этого обратного вызова до создания в обратный вызов после создания.

Возвращаемое значение:

    FLT_PREOP_SUCCESS_WITH_CALLBACK - Если это не наш процесс пользовательского режима.
    FLT_PREOP_SUCCESS_NO_CALLBACK — Все остальные потоки.

--*/
{
    UNREFERENCED_PARAMETER( FltObjects );
    UNREFERENCED_PARAMETER( CompletionContext = NULL );

    PAGED_CODE();

    //
    // Проверяем, наш ли это пользовательский процесс
    //

    if (IoThreadToProcess( Data->Thread ) == MarkReaderData.UserProcess) {

        DbgPrint( "MarkReader: allowing create for trusted process \n" );

        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }
    // Получить PID процесса, пославшего запрос на доступ.
    //UINT64  pid = (UINT64)GetIOProcessId(Data);
    //if (pid == IgnoreIRP_Pid)
    //{
    //    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_TRACE_LEVEL, "Ignore friendly PID: %d\n", IgnoreIRP_Pid);
    //    return FLT_PREOP_SUCCESS_NO_CALLBACK;
    //}


    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}


BOOLEAN
MarkReaderCheckExtension (
    _In_ PUNICODE_STRING Extension
    )
/*++

Описание функции:

    Проверяет, является ли это расширение имени файла чем-то, что нас интересует

Аргументы

    Extension - Указатель на расширение имени файла

Возвращаемое значение

    TRUE - Да, нас интересует
    FALSE - Нет
--*/
{
    ULONG count;

    if (Extension->Length == 0) {

        return FALSE;
    }

    //
    //  Проверим, соответствует ли оно какому-либо расширению из нашего статического списка.
    //

    for (count = 0; count < ScannedExtensionCount; count++) {

        if (RtlCompareUnicodeString( Extension, ScannedExtensions + count, TRUE ) == 0) {

            //
            //  Совпадение. Нас интересует этот файл
            //

            return TRUE;
        }
    }

    return FALSE;
}

FLT_POSTOP_CALLBACK_STATUS
MarkReaderPostCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    )
/*++

Описание функции:

    Обратный вызов после создания. Мы не можем сканировать файл, пока создание не
    перешло в файловую систему, так как в противном случае файловая система не будет готова
    прочитать файл для нас.

Аргументы:

    Data - Структура, описывающая параметры операции.
    FltObject - Структура, описывающая объекты, затронутые этой
    операцией.

    CompletionContext - Контекст операции, переданный из обратного вызова перед созданием.
    Flags - Флаги, сообщающие, почему мы получаем этот обратный вызов после операции.

Возвращаемое значение:

    FLT_POSTOP_FINISHED_PROCESSING - можно ли открыть файл или мы хотим
    запретить доступ к этому файлу, поэтому отменяем открытие

--*/
{
    PMARK_READER_STREAM_HANDLE_CONTEXT MarkReaderContext;
    FLT_POSTOP_CALLBACK_STATUS returnStatus = FLT_POSTOP_FINISHED_PROCESSING;
    PFLT_FILE_NAME_INFORMATION nameInfo;
    NTSTATUS status;
    BOOLEAN locRight, scanFile;

    UNREFERENCED_PARAMETER( CompletionContext );
    UNREFERENCED_PARAMETER( Flags );

    //
    //  Если это создание в любом случае не удалось, не беспокоимся о сканировании сейчас.
    //

    if (!NT_SUCCESS( Data->IoStatus.Status ) ||
        (STATUS_REPARSE == Data->IoStatus.Status)) {

        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    //
    //  Проверим, интересует ли нас этот файл.
    //

    status = FltGetFileNameInformation( Data,
                                        FLT_FILE_NAME_NORMALIZED |
                                            FLT_FILE_NAME_QUERY_DEFAULT,
                                        &nameInfo );

    if (!NT_SUCCESS( status )) {

        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    FltParseFileNameInformation( nameInfo );

    //
    //  Проверим, соответствует ли расширение списку интересующих нас расширений.
    //

    scanFile = MarkReaderCheckExtension( &nameInfo->Extension );

    //
    //  Выведем информация об имени файла
    //

    FltReleaseFileNameInformation( nameInfo );

    if (!scanFile) {

        //
        //  Не то расширение, которое нас интересует
        //

        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    (VOID) MarkReaderpScanFileInUserMode( FltObjects->Instance,
                                       FltObjects->FileObject,
                                       &locRight );

    if (!locRight) {

        //
        //  Попросим менеджера фильтров отменить создание.
        //

        DbgPrint( "MarkReader: Access denied, undoing create \n" );

        FltCancelFileOpen( FltObjects->Instance, FltObjects->FileObject );

        Data->IoStatus.Status = STATUS_ACCESS_DENIED;
        Data->IoStatus.Information = 0;

        returnStatus = FLT_POSTOP_FINISHED_PROCESSING;

    } else if (FltObjects->FileObject->WriteAccess) {

        //
        //
        //  Создание запросило доступ на запись, отметим для повторного сканирования файла. 
        //  Выделим контекст.
        //

        status = FltAllocateContext( MarkReaderData.Filter,
                                     FLT_STREAMHANDLE_CONTEXT,
                                     sizeof(MARK_READER_STREAM_HANDLE_CONTEXT),
                                     PagedPool,
                                     &MarkReaderContext );

        if (NT_SUCCESS(status)) {

            //
            //  Установим контекст дескриптора.
            //

            MarkReaderContext->RescanRequired = TRUE;

            (VOID) FltSetStreamHandleContext( FltObjects->Instance,
                                              FltObjects->FileObject,
                                              FLT_SET_CONTEXT_REPLACE_IF_EXISTS,
                                              MarkReaderContext,
                                              NULL );

            //
            //  Обычно мы проверяем результаты FltSetStreamHandleContext на предмет различных случаев ошибок. 
            //  Однако единственный статус ошибки, который может быть возвращен в этом случае, скажет нам, 
            //  что контексты не поддерживаются. Даже если мы получили эту ошибку, мы просто хотим освободить 
            //  контекст сейчас, и это освободит эту память, если она не была успешно установлена.
            
            //
            //  Освободим нашу ссылку на контекст (набор добавляет ссылку)
            //

            FltReleaseContext( MarkReaderContext );
        }
    }

    return returnStatus;
}


FLT_PREOP_CALLBACK_STATUS
MarkReaderPreCleanup (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Описание функции:

    Обратный вызов перед очисткой. Если этот файл был открыт для записи, мы хотим
    пересканировать его сейчас.

Аргументы:

    Data - Структура, описывающая параметры операции.

    FltObject - Структура, описывающая объекты, затронутые этой
    операцией.

    CompletionContext - Выходной параметр, который можно использовать для передачи контекста
    из этого обратного вызова перед очисткой в ​​обратный вызов после очистки.

Возвращаемое значение:

    Всегда FLT_PREOP_SUCCESS_NO_CALLBACK.

--*/
{
    NTSTATUS status;
    PMARK_READER_STREAM_HANDLE_CONTEXT context;
    BOOLEAN safe;

    UNREFERENCED_PARAMETER( Data );
    UNREFERENCED_PARAMETER( CompletionContext );

    status = FltGetStreamHandleContext( FltObjects->Instance,
                                        FltObjects->FileObject,
                                        &context );

    if (NT_SUCCESS( status )) {

        if (context->RescanRequired) {

            (VOID) MarkReaderpScanFileInUserMode( FltObjects->Instance,
                                               FltObjects->FileObject,
                                               &safe );

            if (!safe) {

                DbgPrint( "MarkReader: access is denied !!!\n" );
            }
        }

        FltReleaseContext( context );
    }


    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}


FLT_PREOP_CALLBACK_STATUS
MarkReaderPreWrite (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Описание функции:

    Обратный вызов перед записью. Мы хотим сканировать то, что записывается сейчас.

Аргументы:

    Data — структура, описывающая параметры операции.

    FltObject — структура, описывающая объекты, затронутые этой
    операцией.

    CompletionContext — выходной параметр, который можно использовать для передачи контекста
    из этого обратного вызова перед записью в обратный вызов после записи.

Возвращаемое значение:

    Всегда FLT_PREOP_SUCCESS_NO_CALLBACK.

--*/
{
    FLT_PREOP_CALLBACK_STATUS returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;
    NTSTATUS status;
    PMARK_READER_NOTIFICATION notification = NULL;
    PMARK_READER_STREAM_HANDLE_CONTEXT context = NULL;
    ULONG replyLength;
    BOOLEAN safe = TRUE;
    PUCHAR buffer;

    UNREFERENCED_PARAMETER( CompletionContext );

    //
    //  Если порт не клиентский, просто проигнорируем эту запись.
    //

    if (MarkReaderData.ClientPort == NULL) {

        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltGetStreamHandleContext( FltObjects->Instance,
                                        FltObjects->FileObject,
                                        &context );

    if (!NT_SUCCESS( status )) {

        //
        //  Нас не интересует этот файл.
        //

        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    //  Используем try-finally для очистки
    //

    try {

        //
        //  Передаем содержимое буфера в пользовательский режим.
        //

        if (Data->Iopb->Parameters.Write.Length != 0) {

            //
            //  Получить адрес буфера пользователя. Если определен MDL, использовать его. 
            //  Если нет, использовать указанный адрес буфера.
            //

            if (Data->Iopb->Parameters.Write.MdlAddress != NULL) {

                buffer = MmGetSystemAddressForMdlSafe( Data->Iopb->Parameters.Write.MdlAddress,
                                                       NormalPagePriority | MdlMappingNoExecute );

                //
                //  Если у нас есть MDL, но мы не смогли получить адрес, у нас закончилась память, 
                //  сообщаем об ошибке.
                //

                if (buffer == NULL) {

                    Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    Data->IoStatus.Information = 0;
                    returnStatus = FLT_PREOP_COMPLETE;
                    leave;
                }

            } else {

                //
                //  Используем буфер пользователя
                //

                buffer  = Data->Iopb->Parameters.Write.WriteBuffer;
            }

            //
            //  В фильтре производственного уровня мы бы фактически позволили пользовательскому режиму 
            //  сканировать файл напрямую. Выделение и освобождение огромных объемов невыгружаемого пула, 
            //  как это, не очень хорошо для производительности системы. Это всего лишь пример!
            //

            notification = ExAllocatePoolZero( NonPagedPool,
                                               sizeof( MARK_READER_NOTIFICATION ),
                                               'nacS' );
            if (notification == NULL) {

                Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                Data->IoStatus.Information = 0;
                returnStatus = FLT_PREOP_COMPLETE;
                leave;
            }

            notification->Size = min( Data->Iopb->Parameters.Write.Length, MARK_READER_READ_BUFFER_SIZE );

            //
            //  Буфер может быть необработанным пользовательским буфером. Защитим доступ к нему
            //

            try  {

                RtlCopyMemory( &notification->Contents,
                               buffer,
                               notification->Size );

            } except( EXCEPTION_EXECUTE_HANDLER ) {

                //
                //  Ошибка доступа к буферу. Завершение ввода-вывода с ошибкой
                //

                Data->IoStatus.Status = GetExceptionCode() ;
                Data->IoStatus.Information = 0;
                returnStatus = FLT_PREOP_COMPLETE;
                leave;
            }

            //
            //  Отправка сообщения в пользовательский режим для указания, что он должен сканировать буфер. 
            //  Нам не нужно синхронизировать отправку и закрытие дескриптора, 
            //  так как FltSendMessage позаботится об этом.   
            //

            replyLength = sizeof( MARK_READER_REPLY );

            status = FltSendMessage( MarkReaderData.Filter,
                                     &MarkReaderData.ClientPort,
                                     notification,
                                     sizeof( MARK_READER_NOTIFICATION ),
                                     notification,
                                     &replyLength,
                                     NULL );

            if (STATUS_SUCCESS == status) {

               safe = ((PMARK_READER_REPLY) notification)->Rights;

           } else {

               //
               //  Не удалось отправить сообщение. Этот пример пропустит ввод/вывод.
               //

               DbgPrint( "MarkReader: couldn't send message to user-mode to scan file, status 0x%X\n", status );
           }
        }

        if (!safe) {

            //
            //  Блокировать эту запись, если не выполняется подкачка ввода-вывода (в результате, конечно, 
            //  этот MarkReader не предотвратит запись в файл с отображением памяти загрязненных строк, 
            //  а только обычную запись). Эффект получения ERROR_ACCESS_DENIED для многих приложений, 
            //  чтобы удалить файл, который они пытаются записать, обычно. Для обработки записи с отображением 
            //  памяти - мы должны сканировать в близкое время (когда мы действительно можем установить,
            //  что объект файла не будет использоваться для дальнейших записей)
            //

            DbgPrint( "MarkReader: access is denied!\n" );

            if (!FlagOn( Data->Iopb->IrpFlags, IRP_PAGING_IO )) {

                DbgPrint( "MarkReader: blocking the write!\n" );

                Data->IoStatus.Status = STATUS_ACCESS_DENIED;
                Data->IoStatus.Information = 0;
                returnStatus = FLT_PREOP_COMPLETE;
            }
        }

    } finally {

        if (notification != NULL) {

            ExFreePoolWithTag( notification, 'nacS' );
        }

        if (context) {

            FltReleaseContext( context );
        }
    }

    return returnStatus;
}

#if (WINVER>=0x0602)

FLT_PREOP_CALLBACK_STATUS
MarkReaderPreFileSystemControl (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    )
/*++

Описание функции:

    Обратный вызов Pre FS Control.

Аргументы:

    Data — структура, описывающая параметры операции.

    FltObject — структура, описывающая объекты, затронутые этой операцией.

    CompletionContext — выходной параметр, который можно использовать для передачи контекста 
    из этого обратного вызова в обратный вызов после записи.

Возвращаемое значение:

    FLT_PREOP_SUCCESS_NO_CALLBACK or FLT_PREOP_COMPLETE

--*/
{
    FLT_PREOP_CALLBACK_STATUS returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;
    NTSTATUS status;
    ULONG fsControlCode;
    PMARK_READER_STREAM_HANDLE_CONTEXT context = NULL;

    UNREFERENCED_PARAMETER( CompletionContext );

    FLT_ASSERT( Data != NULL );
    FLT_ASSERT( Data->Iopb != NULL );

    //
    //  Если порт не клиентский, просто проигнорируем эту запись.
    //

    if (MarkReaderData.ClientPort == NULL) {

        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    status = FltGetStreamHandleContext( FltObjects->Instance,
                                        FltObjects->FileObject,
                                        &context );

    if (!NT_SUCCESS( status )) {

        //
        //  Нас не интересует этот файл.
        //

        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    //
    //  Используем try-finally для очистки
    //

    try {

        fsControlCode = Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;

        if (fsControlCode == FSCTL_OFFLOAD_WRITE) {

            //
            //  MarkReader не может получить доступ к данным в этом запросе на разгрузку записи. 
            //  В фильтре производственного уровня мы бы фактически позволили пользовательскому режиму
            //  сканировать файл после завершения разгрузочной записи (при очистке и т. д.). Поскольку это
            //  всего лишь пример, заблокируйте разгрузочную запись с помощью STATUS_ACCESS_DENIED,
            //  хотя это неприемлемое поведение производственного уровня.
            //

            DbgPrint( "MarkReader: blocking the offload write\n" );

            Data->IoStatus.Status = STATUS_ACCESS_DENIED;
            Data->IoStatus.Information = 0;

            returnStatus = FLT_PREOP_COMPLETE;
        }

    } finally {

        if (context) {

            FltReleaseContext( context );
        }
    }

    return returnStatus;
}

#endif

//////////////////////////////////////////////////////////////////////////
//  Локальные функции поддержки
//
/////////////////////////////////////////////////////////////////////////

NTSTATUS
MarkReaderpScanFileInUserMode (
    _In_ PFLT_INSTANCE Instance,
    _In_ PFILE_OBJECT FileObject,
    _Out_ PBOOLEAN Rights
    )
/*++

Описание функции:

    Эта функция вызывается для отправки запроса в пользовательский режим для сканирования указанного
    файла и сообщения нашему вызывающему, безопасно ли открывать этот файл.

    Обратите внимание, что если сканирование не удается, мы устанавливаем Rights в TRUE. Сканирование может не сработать,
    потому что служба не запущена, или, возможно, потому что это создание/очистка
    для каталога, и нет данных для чтения и сканирования.

    Если мы не сможем создать, когда служба не запущена, возникнет
    проблема с самозагрузкой — как мы вообще загрузим .exe для службы?

Аргументы:

    Instance — дескриптор экземпляра фильтра для MarkReader на этом томе.

    FileObject — файл для сканирования.

    Rights — установите в FALSE, если доступ запрещен.

Возвращаемое значение:

    Статус операции, скорее всего, STATUS_SUCCESS. Распространенным статусом сбоя,
    вероятно, будет STATUS_INSUFFICIENT_RESOURCES.

--*/

{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID buffer = NULL;
    ULONG bytesRead;
    PMARK_READER_NOTIFICATION notification = NULL;
    FLT_VOLUME_PROPERTIES volumeProps;
    LARGE_INTEGER offset;
    ULONG replyLength, length;
    PFLT_VOLUME volume = NULL;

    *Rights = TRUE;

    //
    //  Если порт не клиентский, просто возвращаем
    //

    if (MarkReaderData.ClientPort == NULL) {

        return STATUS_SUCCESS;
    }

    try {

        //
        //  Получим volume объект.
        //

        status = FltGetVolumeFromInstance( Instance, &volume );

        if (!NT_SUCCESS( status )) {

            leave;
        }

        //
        //  Определим размер сектора. Некэшированный ввод-вывод может быть выполнен только при смещениях
        //  размера сектора и с длинами, кратными размеру сектора. Более эффективный способ — сделать этот
        //  вызов один раз и запомнить размер сектора в процедуре настройки экземпляра и настроить контекст
        //  экземпляра, где мы можем его кэшировать.
        //

        status = FltGetVolumeProperties( volume,
                                         &volumeProps,
                                         sizeof( volumeProps ),
                                         &length );
        //
        //  Может быть возвращено значение STATUS_BUFFER_OVERFLOW, однако нам нужны только свойства,
        //  а не имена, поэтому мы проверяем только наличие статуса ошибки.
        //

        if (NT_ERROR( status )) {

            leave;
        }

        length = max( MARK_READER_READ_BUFFER_SIZE, volumeProps.SectorSize );

        //
        //  Используем небуферизованный ввод-вывод, поэтому выделяем выровненный пул
        //

        buffer = FltAllocatePoolAlignedWithTag( Instance,
                                                NonPagedPool,
                                                length,
                                                'nacS' );

        if (NULL == buffer) {

            status = STATUS_INSUFFICIENT_RESOURCES;
            leave;
        }

        notification = ExAllocatePoolZero( NonPagedPool,
                                           sizeof( MARK_READER_NOTIFICATION ),
                                           'nacS' );

        if(NULL == notification) {

            status = STATUS_INSUFFICIENT_RESOURCES;
            leave;
        }

        //
        //  Прочитать начало файла и передать его содержимое в пользовательский режим.
        //

        offset.QuadPart = bytesRead = 0;
        status = FltReadFile( Instance,
                              FileObject,
                              &offset,
                              length,
                              buffer,
                              FLTFL_IO_OPERATION_NON_CACHED |
                              FLTFL_IO_OPERATION_DO_NOT_UPDATE_BYTE_OFFSET,
                              &bytesRead,
                              NULL,
                              NULL );

        if (NT_SUCCESS( status ) && (0 != bytesRead)) {

            notification->Size = (ULONG) bytesRead;

            //
            //  Копировать только столько, сколько может вместить буфер.
            //

            RtlCopyMemory( &notification->Contents,
                           buffer,
                           min( notification->Size, MARK_READER_READ_BUFFER_SIZE ) );

            replyLength = sizeof( MARK_READER_REPLY );

            status = FltSendMessage( MarkReaderData.Filter,
                                     &MarkReaderData.ClientPort,
                                     notification,
                                     sizeof(MARK_READER_NOTIFICATION),
                                     notification,
                                     &replyLength,
                                     NULL );

            if (STATUS_SUCCESS == status) {

                *Rights = ((PMARK_READER_REPLY) notification)->Rights;

            } else {

                //
                //  Не удалось отправить сообщение.
                //

                DbgPrint( "MarkReader: couldn't send message to user-mode to scan file, status 0x%X\n", status );
            }
        }

    } finally {

        if (NULL != buffer) {

            FltFreePoolAlignedWithTag( Instance, buffer, 'nacS' );
        }

        if (NULL != notification) {

            ExFreePoolWithTag( notification, 'nacS' );
        }

        if (NULL != volume) {

            FltObjectDereference( volume );
        }
    }

    return status;
}

