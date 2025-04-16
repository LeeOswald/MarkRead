#pragma once
/*++

��� ������:

    common.h

��������:

    ������������ ����, ���������� ���������, ����������� �����,
    ���������, ���������� ���������� � ��������� �������, �������
    ��������� ������������ ����� � ���������������� �������.

���������:

    ���� � ���������������� �����

--*/

//
//  ��� �����, ������������� ��� �����
//

const PWSTR MarkReaderPortName = L"\\MarkReaderPort";


#define MARK_READER_READ_BUFFER_SIZE   1024

typedef struct _MARK_READER_NOTIFICATION {

    ULONG Size;
    ULONG Reserved;             // ��� ������������ ��������� ����������� �� ������� ������
    UCHAR Contents[MARK_READER_READ_BUFFER_SIZE];
    
} MARK_READER_NOTIFICATION, *PMARK_READER_NOTIFICATION;

typedef struct _MARK_READER_REPLY {

    UCHAR Rights;
    
} MARK_READER_REPLY, *PMARK_READER_REPLY;



