#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include "resource.h"

#define BUFSIZE 4096
#define NICKNAMESIZE 256
#define SERVERPORT 1222

char buf[BUFSIZE + 1]; // 데이터 송수신 버퍼
bool issetnickname = false;

//데이터 송수신시에 사용할 프로토콜
enum PROTOCOL
{
	CHATINFOREQ = 1, 
	ADDRREQ,
	CHATMSG,
	OUTMSG
};

//데이터 송수신팩
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
HWND hSendButton, hLeaveButton; // 보내기 버튼, 방나가기 버튼
HWND hEdit1, hEdit2; // 에디트 컨트롤
HANDLE hReadEvent, hWriteEvent; // 이벤트

// 소켓 함수 오류 출력 후 종료
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

// 소켓 함수 오류 출력
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

// 에디트 컨트롤 출력 함수
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
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	// 이벤트 생성
	hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	// 소켓 통신 스레드 생성
	CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);

	// 대화상자 생성
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_INITDIALOG:	// awake();
		hEdit1 = GetDlgItem(hDlg, IDC_EDIT1);
		hEdit2 = GetDlgItem(hDlg, IDC_EDIT2);
		hSendButton = GetDlgItem(hDlg, IDOK);
		hLeaveButton = GetDlgItem(hDlg, IDLEAVE);
		SendMessage(hEdit1, EM_SETLIMITTEXT, BUFSIZE, 0);
		EnableWindow(hLeaveButton, FALSE); // 방 나가기 버튼 비활성화
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			ZeroMemory(buf, sizeof(buf));
			WaitForSingleObject(hReadEvent, INFINITE); // 읽기 완료 대기
			GetDlgItemTextA(hDlg, IDC_EDIT1, buf, BUFSIZE + 1);
			if (!issetnickname)	//닉네임 설정
				sprintf(RequestPacket.Nname, "[%s]", buf);
			SetEvent(hWriteEvent); // 쓰기 완료 알림
			SetFocus(hEdit1); // 키보드 포커스 전환
			SendMessage(hEdit1, EM_SETSEL, 0, -1); // 텍스트 전체 선택
			return TRUE;
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL); // 대화상자 닫기
			//closesocket(sock); // 소켓 닫기
			return TRUE;
		case IDLEAVE:
			LPCTSTR str = TEXT("Quit");
			//메시지를 보내는 함수, winproc를 직접 호출한다. 처리할동안 시스템은 정지한다.
			SendMessage(hEdit1, WM_SETTEXT, 0, (LPARAM)str);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

//TCP 클라이언트 시작 부분
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

	// 통신
	while (1)
	{
		EnableWindow(hLeaveButton, FALSE); // 방 나가기 버튼 비활성화
		LPCTSTR inputnick = TEXT("닉네임입력");
		//메시지를 보내는 함수, winproc를 직접 호출한다. 처리할동안 시스템은 정지한다.
		SendMessage(hEdit1, WM_SETTEXT, 0, (LPARAM)inputnick);
		SendMessage(hEdit1, EM_SETSEL, 0, -1); // 텍스트 전체 선택

		issetnickname = false;
		WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 대기
		if (strlen(buf) == 0) // 문자열 길이가 0이면 보내지 않음
		{
			SetEvent(hReadEvent); // 읽기 완료 알림
			continue;
		}

		// CHATINFO 요청 PACKET SEND
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

		// 받은 데이터 출력
		buf[retval] = '\0';
		DisplayText("%s\r\n", buf);
		issetnickname = true;

		SetEvent(hReadEvent); // 읽기 완료 알림
		WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 대기

		if (strlen(buf) == 0) // 문자열 길이가 0이면 보내지 않음
		{
			SetEvent(hReadEvent); // 읽기 완료 알림
			continue;
		}

		// PROTOCOL 변경후 입력한 방번호 send
		RequestPacket.protocol = ADDRREQ;
		RequestPacket.Data = (buf[0] - 48);
		retval = send(server_sock, (char*)&RequestPacket, sizeof(RequestPacket), 0);
		if (retval == SOCKET_ERROR) {
			err_quit("recvfrom()");
		}

		SOCKADDR_IN multicastaddr, localaddr;
		ZeroMemory(&localaddr, sizeof(localaddr));

		// 방번호따른 주소 구조체 recv
		retval = recv(server_sock, (char *)& multicastaddr, sizeof(SOCKADDR), 0);
		if (retval == SOCKET_ERROR) {
			err_display("recvfrom()");
		}

		SetEvent(hReadEvent); // 읽기 완료 알림

		// 채팅 전용 UDP소켓
		SOCKET chat_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (chat_sock == INVALID_SOCKET) err_quit("socket()");

		// 채팅창 수신 전용 스레드 생성
		DWORD ThreadId;
		hRecvThread = CreateThread(NULL, 0, ChatProcess, (LPVOID)chat_sock, 0, &ThreadId);
		
		// REUSEADDR설정
		BOOL optval = true;
		retval = setsockopt(chat_sock, SOL_SOCKET, SO_REUSEADDR,
			(char*)&optval, sizeof(optval));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		// 주소설정 후 bind()
		localaddr.sin_family = AF_INET;
		localaddr.sin_port = multicastaddr.sin_port;
		localaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

		retval = bind(chat_sock, (SOCKADDR*)&localaddr, sizeof(localaddr));
		if (retval == SOCKET_ERROR) err_quit("bind()");

		// 멀티캐스트 그룹 가입
		struct ip_mreq mreq;
		memcpy(&mreq.imr_multiaddr.s_addr, 
			&multicastaddr.sin_addr, sizeof(multicastaddr.sin_addr));
		mreq.imr_interface.s_addr = inet_addr("127.0.0.1");

		retval = setsockopt(chat_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		int protocol = 0;

		EnableWindow(hLeaveButton, TRUE); // 방 나가기 버튼 활성화

		// 채팅창
		while (1)
		{
			WaitForSingleObject(hWriteEvent, INFINITE); // 쓰기 완료 대기
			if (strlen(buf) == 0) // 문자열 길이가 0이면 보내지 않음
			{
				SetEvent(hReadEvent); // 읽기 완료 알림
				continue;
			}

			//printf("%s", Nname);
			char msg[BUFSIZE + NICKNAMESIZE];
			memcpy(msg, &buf, sizeof(buf));

			//방 나가기 버튼 눌렀을경우
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
			
				Sleep(500);	//서버로부터 recvfrom을 받은후 해당 스레드를 종료하기위함

				// to multicast
				retval = sendto(chat_sock, buf, size, 0, (SOCKADDR*)&multicastaddr, sizeof(multicastaddr));
				if (retval == SOCKET_ERROR) {
					err_display("recvfrom()");
					continue;
				}
				SetEvent(hReadEvent); // 읽기 완료 알림
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
			SetEvent(hReadEvent); // 읽기 완료 알림
		}

		// 멀티캐스트 그룹 탈퇴
		retval = setsockopt(chat_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
			(char*)&mreq, sizeof(mreq));
		if (retval == SOCKET_ERROR) err_quit("setsockopt()");

		// closesocket()
		closesocket(chat_sock);
	}

	closesocket(server_sock);

	return 0;
}

// 멀티캐스트 recvform 전용 스레드
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