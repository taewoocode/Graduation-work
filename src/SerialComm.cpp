#include "pch.h"
#include "SerialComm.h"


UINT CSerialComm::ThreadProc(LPVOID lParam)
{
	CSerialComm* pSerialData = (CSerialComm*)lParam;

	DWORD		dwEvent;
	OVERLAPPED	os;
	BOOL		bOk = TRUE;
	BYTE		buff[2048];	 // �б� ����
	DWORD		dwRead;	 // ���� ����Ʈ��.
	BYTE		Filter_buff[2048];	 // �б� ����
	int			Filter_buff_len = 0;

	// Event, OS ����.
	memset(&os, 0, sizeof(OVERLAPPED));
	if (!(os.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
		bOk = FALSE;
	if (!SetCommMask(pSerialData->m_hComm, EV_RXCHAR))
		bOk = FALSE;
	if (!bOk)
	{
		AfxMessageBox(_T("Error while creating ThreadWatchComm, ") + pSerialData->m_sPortName);
		return FALSE;
	}

	CString str;
	// ��Ʈ�� �����ϴ� ����.
	while (pSerialData->m_bConnected)
	{
		dwEvent = 0;

		// ��Ʈ�� ���� �Ÿ��� �ö����� ��ٸ���.
		WaitCommEvent(pSerialData->m_hComm, &dwEvent, NULL);

		if ((dwEvent & EV_RXCHAR) == EV_RXCHAR)
		{
			dwRead = pSerialData->ReadComm(buff, 2048);

			if (dwRead != 0)
			{
				char key = buff[dwRead - 1];

				if (buff[dwRead - 1] == 10 || buff[dwRead - 1] == 13)
				{
					memcpy(Filter_buff + Filter_buff_len, buff, dwRead);
					Filter_buff_len += dwRead;

					Filter_buff[Filter_buff_len] = '\0';

					for (int i = 0; i < Filter_buff_len; i++)
						pSerialData->QueuePutByte(Filter_buff[i]);

					pSerialData->m_RecvMessage = CString(Filter_buff);

					//	�о� ������ �޽����� ������.
					::PostMessage(pSerialData->hRecvCom, WM_COMM_READ, pSerialData->m_wPortID, 0);

					Filter_buff_len = 0;
					memset(Filter_buff, 0x00, 2048);
				}
				else
				{
					memcpy(Filter_buff + Filter_buff_len, buff, dwRead);
					Filter_buff_len += dwRead;
				}
			}

		}
	}

	CloseHandle(os.hEvent);
	pSerialData->m_hThreadWatchComm = NULL;

	return TRUE;
}


CSerialComm::CSerialComm(HWND hwnd)
{
	hRecvCom = hwnd;
}

CSerialComm::~CSerialComm()
{

}

BOOL CSerialComm::QueueGetByte(BYTE* pb)
{
	if (QueueGetSize() == 0)
	{
		QueueClear();
		return FALSE;
	}
	*pb = m_Queuebuff[m_QueueTail++];
	m_QueueTail %= BUFF_SIZE;
	return TRUE;
}

BOOL CSerialComm::QueuePutByte(BYTE b)
{
	if (QueueGetSize() == (BUFF_SIZE - 1)) return FALSE;
	m_Queuebuff[m_QueueHead++] = b;
	m_QueueHead %= BUFF_SIZE;
	return TRUE;
}

long CSerialComm::QueueGetSize()
{
	return (m_QueueHead - m_QueueTail + BUFF_SIZE) % BUFF_SIZE;
}

void CSerialComm::QueueClear()
{
	m_QueueHead = m_QueueTail = 0;
	memset(m_Queuebuff, 0, BUFF_SIZE);
}

BOOL CSerialComm::OpenPort(DWORD dwBaud, WORD wPortID)
{
	// Local ����.
	COMMTIMEOUTS	timeouts;
	DCB				dcb;
	DWORD			dwThreadID;

	CString sPortName;

	if (wPortID < 10)
	{
		sPortName.Format(_T("COM%d"), wPortID);
	}
	else
	{
		sPortName.Format(_T("\\\\.\\COM%d"), wPortID);
	}

	QueueClear();

	// ���� �ʱ�ȭ
	m_bConnected = FALSE;
	m_wPortID = wPortID; // COM1-> 0, COM2->1,,,,,

	// overlapped structure ���� �ʱ�ȭ.
	m_osRead.Offset = 0;
	m_osRead.OffsetHigh = 0;
	if (!(m_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
		return FALSE;
	m_osWrite.Offset = 0;
	m_osWrite.OffsetHigh = 0;
	if (!(m_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
		return FALSE;

	// ��Ʈ ����
	m_sPortName = sPortName;
	m_hComm = CreateFile(m_sPortName,
		GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL);
	if (m_hComm == (HANDLE)-1)
		return FALSE;

	// ��Ʈ ���� ����.

	// EV_RXCHAR event ����
	SetCommMask(m_hComm, EV_RXCHAR);

	// InQueue, OutQueue ũ�� ����.
	SetupComm(m_hComm, 4096, 4096);

	// ��Ʈ ����.
	PurgeComm(m_hComm,
		PURGE_TXABORT | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_RXCLEAR);

	// timeout ����.
	timeouts.ReadIntervalTimeout = 0xFFFFFFFF;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.WriteTotalTimeoutMultiplier = 2 * CBR_9600 / dwBaud;
	timeouts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(m_hComm, &timeouts);

	// dcb ����
	dcb.DCBlength = sizeof(DCB);
	GetCommState(m_hComm, &dcb);	// ���� ���� ����.
	dcb.BaudRate = dwBaud;
	dcb.ByteSize = 8;
	dcb.Parity = 0;
	dcb.StopBits = 0;
	dcb.fInX = dcb.fOutX = 1;		// Xon, Xoff ���.
	dcb.XonChar = ASCII_XON;
	dcb.XoffChar = ASCII_XOFF;
	dcb.XonLim = 100;
	dcb.XoffLim = 100;
	if (!SetCommState(m_hComm, &dcb))	return FALSE;

	// ��Ʈ ���� ������ ����.
	m_bConnected = TRUE;
	m_hThreadWatchComm = CreateThread(NULL, 0,
		(LPTHREAD_START_ROUTINE)ThreadProc, this, 0, &dwThreadID);
	if (!m_hThreadWatchComm)
	{
		ClosePort();
		return FALSE;
	}

	return TRUE;
}

void CSerialComm::ClosePort()
{
	m_bConnected = FALSE;
	SetCommMask(m_hComm, 0);
	PurgeComm(m_hComm,
		PURGE_TXABORT | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_RXCLEAR);
	CloseHandle(m_hComm);
}

DWORD CSerialComm::WriteComm(BYTE* pBuff, DWORD nToWrite)
{
	DWORD	dwWritten, dwError, dwErrorFlags;
	COMSTAT	comstat;

	if (!WriteFile(m_hComm, pBuff, nToWrite, &dwWritten, &m_osWrite))
	{
		if (GetLastError() == ERROR_IO_PENDING)
		{
			// ���� ���ڰ� ���� �ְų� ������ ���ڰ� ���� ���� ��� Overapped IO��
			// Ư���� ���� ERROR_IO_PENDING ���� �޽����� ���޵ȴ�.
			//timeouts�� ������ �ð���ŭ ��ٷ��ش�.
			while (!GetOverlappedResult(m_hComm, &m_osWrite, &dwWritten, TRUE))
			{
				dwError = GetLastError();
				if (dwError != ERROR_IO_INCOMPLETE)
				{
					ClearCommError(m_hComm, &dwErrorFlags, &comstat);
					break;
				}
			}
		}
		else
		{
			dwWritten = 0;
			ClearCommError(m_hComm, &dwErrorFlags, &comstat);
		}
	}

	return dwWritten;
}

DWORD CSerialComm::ReadComm(BYTE* pBuff, DWORD nToRead)
{
	DWORD	dwRead, dwError, dwErrorFlags;
	COMSTAT	comstat;

	//----------------- system queue�� ������ byte���� �̸� �д´�.
	ClearCommError(m_hComm, &dwErrorFlags, &comstat);
	dwRead = comstat.cbInQue;

	if (dwRead > 0)
	{
		if (!ReadFile(m_hComm, pBuff, nToRead, &dwRead, &m_osRead))
		{
			if (GetLastError() == ERROR_IO_PENDING)
			{
				//--------- timeouts�� ������ �ð���ŭ ��ٷ��ش�.
				while (!GetOverlappedResult(m_hComm, &m_osRead, &dwRead, TRUE))
				{
					dwError = GetLastError();
					if (dwError != ERROR_IO_INCOMPLETE)
					{
						ClearCommError(m_hComm, &dwErrorFlags, &comstat);
						break;
					}
				}
			}
			else
			{
				dwRead = 0;
				ClearCommError(m_hComm, &dwErrorFlags, &comstat);
			}
		}
	}

	return dwRead;
}

void CSerialComm::ThreadStart()
{
	if (NULL == m_pThread)
	{
		m_thread_flag = FALSE;
		m_pThread = AfxBeginThread(ThreadProc, this);
	}
}

void CSerialComm::ThreadStop()
{
	if (NULL == m_pThread)	return;
	HANDLE nThread = m_pThread->m_hThread;
	::SuspendThread(nThread);
}

void CSerialComm::ThreadRestart()
{
	if (NULL == m_pThread)	return;
	HANDLE nThread = m_pThread->m_hThread;
	::ResumeThread(nThread);
}

void CSerialComm::ThreadTerminate()
{
	SleepClose();
	if (NULL == m_pThread)	return;

	m_thread_flag = TRUE;
	HANDLE nThread = m_pThread->m_hThread;
	DWORD nExitCode = 0;
	if (0 != ::TerminateThread(nThread, nExitCode))
	{
		m_pThread = NULL;
	}
}

void CSerialComm::SetThreadFlag(BOOL arg_flag)
{
	m_thread_flag = arg_flag;
}

void CSerialComm::SleepSetup(int arg_sleep)
{
	m_Sleep_time = arg_sleep;

	if (NULL == m_hSleep)
	{
		m_hSleep = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
}

void CSerialComm::SleepThread()
{
	ASSERT(m_Sleep_time >= 0);
	if (NULL == m_hSleep)
	{
		m_hSleep = CreateEvent(NULL, TRUE, FALSE, NULL);
		Sleep(m_Sleep_time);
	}
	else
	{
		WaitForSingleObject(m_hSleep, m_Sleep_time);
	}
}

void CSerialComm::SleepClose()
{
	if (NULL != m_hSleep)
	{
		if (0 != CloseHandle(m_hSleep))
		{
			m_hSleep = NULL;
		}
	}
}


int CSerialComm::is_connect()
{
	// TODO: ���⿡ ���� �ڵ� �߰�.

	int a_flag = 0;
	DWORD		dwEvent;

	WaitCommEvent(m_hComm, &dwEvent, NULL);

	if (dwEvent == 0)
		a_flag = 1;

	return a_flag;
}
