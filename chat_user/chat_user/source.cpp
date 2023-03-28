#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define BUFSIZE 4096
#define NICKNAMESIZE 256
#define SERVERPORT 1222

char buf[BUFSIZE + 1]; // ������ �ۼ��� ����
bool issetnickname = false;

//������ �ۼ��Žÿ� ����� ��������
enum PROTOCOL
{
	CHATINFOREQ = 1, 
	ADDRREQ,
	CHATMSG,
	OUTMSG
};

//������ �ۼ�����
struct _RequestPacket
{
	PROTOCOL protocol;
	int Data;
	char Nname[NICKNAMESIZE];
}RequestPacket;

DWORD CALLBACK ChatProcess(LPVOID prt);
DWORD WINAPI ClientMain(LPVOID arg);
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

HANDLE hRecvThread;
HWND hSendButton, hLeaveButton; // ������ ��ư, �泪���� ��ư
HWND hEdit1, hEdit2; // ����Ʈ ��Ʈ��
HANDLE hReadEvent, hWriteEvent; // �̺�Ʈ

// ���� �Լ� ���� ��� �� ����
void err_quit(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(-1);
}

// ���� �Լ� ���� ���
void err_display(char *msg)
{
	LPVOID lpMsgBuf;
	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER|
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (LPCTSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

// ����Ʈ ��Ʈ�� ��� �Լ�
void DisplayText(const char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	char cbuf[BUFSIZE * 2];
	vsprintf(cbuf, fmt, arg);
	va_end(arg);

	int nLength = GetWindowTextLength(hEdit2);
	SendMessage(hEdit2, EM_SETSEL, nLength, nLength);
	SendMessageA(hEdit2, EM_REPLACESEL, FALSE, (LPARAM)cbuf);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	// �̺�Ʈ ����
	hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// ���� ��� ������ ����
	CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);

	// ��ȭ���� ����
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// ���� ����
	WSACleanup();
	return 0;
}

// ��ȭ���� ���ν���
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_INITDIALOG:	// awake();
		hEdit1 = GetDlgItem(hDlg, IDC_EDIT1);
		hEdit2 = GetDlgItem(hDlg, IDC_EDIT2);
		hSendButton = GetDlgItem(hDlg, IDOK);
		hLeaveButton = GetDlgItem(hDlg, IDLEAVE);
		SendMessage(hEdit1, EM_SETLIMITTEXT, BUFSIZE, 0);
		EnableWindow(hLeaveButton, FALSE); // �� ������ ��ư ��Ȱ��ȭ
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			ZeroMemory(buf, sizeof(buf));
			WaitForSingleObject(hReadEvent, INFINITE); // �б� �Ϸ� ���
			GetDlgItemTextA(hDlg, IDC_EDIT1, buf, BUFSIZE + 1);
			if (!issetnickname)	//�г��� ����
				sprintf(RequestPacket.Nname, "[%s]", buf);
			SetEvent(hWriteEvent); // ���� �Ϸ� �˸�
			SetFocus(hEdit1); // Ű���� ��Ŀ�� ��ȯ
			SendMessage(hEdit1, EM_SETSEL, 0, -1); // �ؽ�Ʈ ��ü ����
			return TRUE;
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL); // ��ȭ���� �ݱ�
			//closesocket(sock); // ���� �ݱ�
			return TRUE;
		case IDLEAVE:
			LPCTSTR str = TEXT("Quit");
			//�޽����� ������ �Լ�, winproc�� ���� ȣ���Ѵ�. ó���ҵ��� �ý����� �����Ѵ�.
			SendMessage(hEdit1, WM_SETTEXT, 0, (LPARAM)str);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

//TCP Ŭ���̾�Ʈ ���� �κ�
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// socket()
	SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(server_sock == INVALID_SOCKET) err_quit("socket()");

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SERVERPORT);
	serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	// connect()
	retval = connect(server_sock, (struct sockaddr*)&serveraddr,
		sizeof(serveraddr));

	SOCKADDR_IN peeraddr;
	int addrlen;
	ZeroMemory(&RequestPacket, sizeof(RequestPacket));	

	// ���
	while (1)
	{
		EnableWindow(hLeaveButton, FALSE); // �� ������ ��ư ��Ȱ��ȭ
		LPCTSTR inputnick = TEXT("�г����Է�");
		//�޽����� ������ �Լ�, winproc�� ���� ȣ���Ѵ�. ó���ҵ��� �ý����� �����Ѵ�.
		SendMessage(hEdit1, WM_SETTEXT, 0, (LPARAM)inputnick);
		SendMessage(hEdit1, EM_SETSEL, 0, -1); // �ؽ�Ʈ ��ü ����

		issetnickname = false;
		WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ���
		if (strlen(buf) == 0) // ���ڿ� ���̰� 0�̸� ������ ����
		{
			SetEvent(hReadEvent); // �б� �Ϸ� �˸�
			continue;
		}

		// CHATINFO ��û PACKET SEND
		RequestPacket.protocol = CHATINFOREQ;
		retval = send(server_sock, (char*)&RequestPacket, sizeof(RequestPacket), 0);
		if (retval == SOCKET_ERROR) {
			err_quit("recvfrom()");
		}

		// CHATINFO RECV
		retval = recv(server_sock, buf, BUFSIZE, 0);
		if (retval == SOCKET_ERROR) {
			err_display("recvfrom()");
			continue;
		}

		// ���� ������ ���
		buf[retval] = '\0';
		DisplayText("%s\r\n", buf);
		issetnickname = true;

		SetEvent(hReadEvent); // �б� �Ϸ� �˸�
		WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ���

		if (strlen(buf) == 0) // ���ڿ� ���̰� 0�̸� ������ ����
		{
			SetEvent(hReadEvent); // �б� �Ϸ� �˸�
			continue;
		}

		// PROTOCOL ������ �Է��� ���ȣ send
		RequestPacket.protocol = ADDRREQ;
		RequestPacket.Data = (buf[0] - 48);
		retval = send(server_sock, (char*)&RequestPacket, sizeof(RequestPacket), 0);
		if (retval == SOCKET_ERROR) {
			err_quit("recvfrom()");
		}

		SOCKADDR_IN multicastaddr, localaddr;
		ZeroMemory(&localaddr, sizeof(localaddr));

		// ���ȣ���� �ּ� ����ü recv
		retval = recv(server_sock, (char *)& multicastaddr, sizeof(SOCKADDR), 0);
		if (retval == SOCKET_ERROR) {
			err_display("recvfrom()");
		}

		SetEvent(hReadEvent); // �б� �Ϸ� �˸�

		// ä�� ���� UDP����
		SOCKET chat_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (chat_sock == INVALID_SOCKET) err_quit("socket()");

		// ä��â ���� ���� ������ ����
		DWORD ThreadId;
		hRecvThread = CreateThread(NULL, 0, ChatProcess, (LPVOID)chat_sock, 0, &ThreadId);
		
		// REUSEADDR����
		BOOL optval = true;
		retval = setsockopt(chat_sock, SOL_SOCKET, SO_REUSEADDR,
			(char*)&optval, sizeof(optval));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		// �ּҼ��� �� bind()
		localaddr.sin_family = AF_INET;
		localaddr.sin_port = multicastaddr.sin_port;
		localaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

		retval = bind(chat_sock, (SOCKADDR*)&localaddr, sizeof(localaddr));
		if (retval == SOCKET_ERROR) err_quit("bind()");

		// ��Ƽĳ��Ʈ �׷� ����
		struct ip_mreq mreq;
		memcpy(&mreq.imr_multiaddr.s_addr, 
			&multicastaddr.sin_addr, sizeof(multicastaddr.sin_addr));
		mreq.imr_interface.s_addr = inet_addr("127.0.0.1");

		retval = setsockopt(chat_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		int protocol = 0;

		EnableWindow(hLeaveButton, TRUE); // �� ������ ��ư Ȱ��ȭ

		// ä��â
		while (1)
		{
			WaitForSingleObject(hWriteEvent, INFINITE); // ���� �Ϸ� ���
			if (strlen(buf) == 0) // ���ڿ� ���̰� 0�̸� ������ ����
			{
				SetEvent(hReadEvent); // �б� �Ϸ� �˸�
				continue;
			}

			//printf("%s", Nname);
			char msg[BUFSIZE + NICKNAMESIZE];
			memcpy(msg, &buf, sizeof(buf));

			//�� ������ ��ư ���������
			if (strcmp(msg, "Quit") == 0)
			{
				int size = 0;
				protocol = OUTMSG;
				memcpy(buf, &protocol, sizeof(protocol));
				size = size + sizeof(int);
				memcpy(buf + sizeof(protocol), &hRecvThread, sizeof(HANDLE));
				size = size + sizeof(HANDLE);


				RequestPacket.protocol = OUTMSG;
				RequestPacket.Data = 0;
			
				// to server
				retval = send(server_sock, (char*)&RequestPacket, sizeof(RequestPacket), 0);
				if (retval == SOCKET_ERROR) {
					err_quit("recvfrom()");
				}
			
				Sleep(500);	//�����κ��� recvfrom�� ������ �ش� �����带 �����ϱ�����

				// to multicast
				retval = sendto(chat_sock, buf, size, 0, (SOCKADDR*)&multicastaddr, sizeof(multicastaddr));
				if (retval == SOCKET_ERROR) {
					err_display("recvfrom()");
					continue;
				}
				SetEvent(hReadEvent); // �б� �Ϸ� �˸�
				break;
			}

			protocol = CHATMSG;
			int size = 0;
			memcpy(buf, &protocol, sizeof(protocol));
			size = size + sizeof(int);
			memcpy(buf + sizeof(protocol), RequestPacket.Nname, strlen(RequestPacket.Nname));
			size = size + strlen(RequestPacket.Nname);
			memcpy(buf + sizeof(protocol) + strlen(RequestPacket.Nname), msg, strlen(msg));
			size = size + strlen(msg);

			retval = sendto(chat_sock, buf, size, 0, (SOCKADDR*)&multicastaddr, sizeof(multicastaddr));
			if (retval == SOCKET_ERROR) {
				err_display("recvfrom()");
				continue;
			}
			SetEvent(hReadEvent); // �б� �Ϸ� �˸�
		}

		// ��Ƽĳ��Ʈ �׷� Ż��
		retval = setsockopt(chat_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
			(char*)&mreq, sizeof(mreq));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		// closesocket()
		closesocket(chat_sock);
	}

	closesocket(server_sock);

	return 0;
}

// ��Ƽĳ��Ʈ recvform ���� ������
DWORD CALLBACK ChatProcess(LPVOID ptr)
{
	SOCKET chat_sock = (SOCKET)ptr;

	SOCKADDR_IN peeraddr;
	int addrlen=sizeof(peeraddr);
	char buf[BUFSIZE];
	char msg[BUFSIZE];
	ZeroMemory(&peeraddr, sizeof(peeraddr));

	while (1)
	{
		int protocol;
		bool endflag = false;
		HANDLE hThreadHandle = NULL;

		ZeroMemory(buf, sizeof(buf));
		int retval = recvfrom(chat_sock, buf, sizeof(buf), 0,(SOCKADDR *)&peeraddr, &addrlen);
		if (retval == SOCKET_ERROR){
			break;
		}

		memcpy(&protocol, buf, sizeof(int));	

		switch (protocol)
		{
		case CHATMSG:
			DisplayText("%s\r\n", buf+sizeof(int));
			break;
		case OUTMSG:
			memcpy(&hThreadHandle, buf + sizeof(int), sizeof(HANDLE));
			if (hThreadHandle == hRecvThread)
			{	
				endflag = true;
				break;
			}
			break;
		}		

		if (endflag)
		{
			break;
		}
	}
	return 0;
}