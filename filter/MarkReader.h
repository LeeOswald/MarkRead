#pragma once
/*++

��� ������:

    MarkReader.h

��������:
    ������������ ����, ���������� ���������, ����������� �����, ���������,
    ���������� ���������� � ��������� �������, ������� ����� ������ ������ ����.

���������:

    ����� ����

--*/


///////////////////////////////////////////////////////////////////////////
//
//  ���������� ����������
//
///////////////////////////////////////////////////////////////////////////


typedef struct _MARK_READER_DATA {

    //
    //  ������, ���������������� ���� �������.
    //

    PDRIVER_OBJECT DriverObject;

    //
    //  ���������� �������, ���������� � ���������� ������ FltRegisterFilter.
    //

    PFLT_FILTER Filter;

    //
    //  ������������ �������� ����������
    //

    PFLT_PORT ServerPort;

    //
    //  ���������������� �������, �������������� � �����
    //

    PEPROCESS UserProcess;

    //
    //  ���������� ���� ��� ����������� � ����������������� ������
    //

    PFLT_PORT ClientPort;

} MARK_READER_DATA, *PMARK_READER_DATA;

extern MARK_READER_DATA MarkReaderData;

typedef struct _MARK_READER_STREAM_HANDLE_CONTEXT {

    BOOLEAN RescanRequired;
    
} MARK_READER_STREAM_HANDLE_CONTEXT, *PMARK_READER_STREAM_HANDLE_CONTEXT;

#pragma warning(push)
#pragma warning(disable:4200) // ��������� �������������� ��� �������� � ��������� ������� �����.

typedef struct _MARK_READER_CREATE_PARAMS {

    WCHAR String[0];

} MARK_READER_CREATE_PARAMS, *PMARK_READER_CREATE_PARAMS;

#pragma warning(pop)


///////////////////////////////////////////////////////////////////////////
//
//  ��������� ��� ������� ������� � ��������, ������������ ��� ����� �������.

//  ���������� � MarkReader.c
//
///////////////////////////////////////////////////////////////////////////
DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry (
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    );

NTSTATUS
MarkReaderUnload (
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags
    );

NTSTATUS
MarkReaderQueryTeardown (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
MarkReaderPreCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

FLT_POSTOP_CALLBACK_STATUS
MarkReaderPostCreate (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_opt_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags
    );

FLT_PREOP_CALLBACK_STATUS
MarkReaderPreCleanup (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

FLT_PREOP_CALLBACK_STATUS 
MarkReaderPreWrite (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

#if (WINVER >= 0x0602)

FLT_PREOP_CALLBACK_STATUS
MarkReaderPreFileSystemControl (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID *CompletionContext
    );

#endif

NTSTATUS
MarkReaderInstanceSetup (
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
    );


