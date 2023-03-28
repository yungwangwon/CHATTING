#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFSIZE 4096
#define SERVERPORT 1222
#define NICKNAMESIZE 256

char buf[BUFSIZE + 1]; // 데이터 송수신 버퍼

DWORD WINAPI ProcessClient(LPVOID arg);

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
};


// 소켓 함수 오류 출력 후 종료
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(-1);
}

// 소켓 함수 오류 출력
void err_display(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (LPCTSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}


int main(int argc, char* argv[])
{

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return -1;

	// 소켓 생성 TCP
	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock == INVALID_SOCKET)
	{
		err_display("sock()");
		return -1;
	}

	// 소켓 주소 구조체 세팅
	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	// bind()
	int retval = bind(listen_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
	{
		err_display("bind()");
		return -1;
	}

	// listen()
	retval = listen(listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		err_display("listen()");
		return -1;
	}

	// 데이터 통신에 사용할 변수
	SOCKET client_sock;
	struct sockaddr_in clientaddr;
	int addrlen;
	HANDLE hThread;

	while (1) 
	{
		// accept()
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_sock, (struct sockaddr*)&clientaddr, &addrlen);
		if (client_sock == INVALID_SOCKET) {
			err_display("listen()");
			break;
		}

		// 접속한 클라이언트 정보 출력
		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
		printf("\r\n[TCP 서버] 클라이언트 접속: IP 주소=%s, 포트 번호=%d\r\n",
			addr, ntohs(clientaddr.sin_port));

		// 스레드 생성
		hThread = CreateThread(NULL, 0, ProcessClient,
			(LPVOID)client_sock, 0, NULL);
		if (hThread == NULL) { closesocket(client_sock); }
		else { CloseHandle(hThread); }
	}

	// closesocket()
	closesocket(listen_sock);

	// 윈속 종료
	WSACleanup();
	return 0;
}

DWORD WINAPI ProcessClient(LPVOID arg)
{
	SOCKET client_sock = (SOCKET)arg;
	struct sockaddr_in clientaddr;

	char addr[INET_ADDRSTRLEN];
	int addrlen;
	int retval;
	PROTOCOL protocol;
	struct ip_mreq mreq;
	char msg[] = "채팅방을 고르세요!!! \r\n1.윤방\r\n2.광방\r\n3.원방\r\n";
	_RequestPacket requestpacket;

	//멀티캐스트 전용 UDP소켓 생성
	SOCKET chat_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (chat_sock == INVALID_SOCKET) err_quit("socket()");

	//REUSEADDR
	BOOL optval = true;
	retval = setsockopt(chat_sock, SOL_SOCKET, SO_REUSEADDR,
		(char*)&optval, sizeof(optval));
	if (retval == SOCKET_ERROR) err_quit("setsockopt()");

	// 클라이언트 정보 얻기
	addrlen = sizeof(clientaddr);
	getpeername(client_sock, (struct sockaddr*)&clientaddr, &addrlen);
	inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

	//방번호에 따른 주소 구조체 
	struct sockaddr_in multiaddr;
	SOCKADDR_IN chatR_1, chatR_2, chatR_3;
	ZeroMemory(&chatR_1, sizeof(chatR_1));
	chatR_1.sin_family = AF_INET;
	chatR_1.sin_port = htons(9001);
	chatR_1.sin_addr.s_addr = inet_addr("235.7.8.9");

	ZeroMemory(&chatR_2, sizeof(chatR_2));
	chatR_2.sin_family = AF_INET;
	chatR_2.sin_port = htons(9002);
	chatR_2.sin_addr.s_addr = inet_addr("235.7.8.10");

	ZeroMemory(&chatR_3, sizeof(chatR_3));
	chatR_3.sin_family = AF_INET;
	chatR_3.sin_port = htons(9003);
	chatR_3.sin_addr.s_addr = inet_addr("235.7.8.11");

	while (1)
	{
		// 데이터 받기
		retval = recv(client_sock, (char *)& requestpacket, sizeof(_RequestPacket), 0);
		if (retval == SOCKET_ERROR) {
			err_display("recv()");
			break;
		}

		//프로토콜에 따른 실행
		switch (requestpacket.protocol)
		{
		case CHATINFOREQ:	//채팅창 요구
			retval = send(client_sock, msg, strlen(msg), 0);
			if (retval == SOCKET_ERROR)
			{
				err_display("recvfrom()");
				continue;
			}

			break;
		case ADDRREQ: {	//방번호 따른 주소 요구
			switch (requestpacket.Data)
			{
			case 1:
				memcpy(&multiaddr, &chatR_1, sizeof(SOCKADDR));
				break;
			case 2:
				memcpy(&multiaddr, &chatR_2, sizeof(SOCKADDR));
				break;
			case 3:
				memcpy(&multiaddr, &chatR_3, sizeof(SOCKADDR));
				break;
			}

			retval = send(client_sock, (char*)&multiaddr, sizeof(SOCKADDR), 0);
			if (retval == SOCKET_ERROR)
			{
				err_display("recvfrom()");
				continue;
			}

			memcpy(&mreq.imr_multiaddr.s_addr,
				&multiaddr.sin_addr, sizeof(multiaddr.sin_addr));
			mreq.imr_interface.s_addr = inet_addr("127.0.0.1");

			retval = setsockopt(chat_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
			if (retval == SOCKET_ERROR) err_quit("setsockopt()2");

			Sleep(500);
			char iomsg[BUFSIZE];
			int iomsgsize = sprintf(iomsg, "%s님이 입장하셨습니다.", requestpacket.Nname);
			int size = 0;

			protocol = CHATMSG;
			memcpy(buf, &protocol, sizeof(protocol));
			size = size + sizeof(int);
			memcpy(buf + sizeof(protocol), iomsg, iomsgsize);
			size = size + iomsgsize;

			retval = sendto(chat_sock, buf, size, 0, (SOCKADDR*)&multiaddr, sizeof(multiaddr));
			if (retval == SOCKET_ERROR) {
				err_display("recvfrom()");
				continue;
			}

			/*printf("[UDP/%s:%d] %d번방 선택\n", inet_ntoa(clientaddr.sin_addr),
				ntohs(clientaddr.sin_port), requestpacket.Data);*/
			break;
		}
		case OUTMSG:	//방 나가기
			char iomsg[BUFSIZE];
			int iomsgsize = sprintf(iomsg, "%s님이 퇴장하셨습니다.", requestpacket.Nname);
			int size = 0;

			// buf pakcing
			protocol = CHATMSG;
			memcpy(buf, &protocol, sizeof(protocol));
			size = size + sizeof(int);
			memcpy(buf + sizeof(protocol), iomsg, iomsgsize);
			size = size + iomsgsize;

			retval = sendto(chat_sock, buf, size, 0, (SOCKADDR*)&multiaddr, sizeof(multiaddr));
			if (retval == SOCKET_ERROR) {
				err_display("recvfrom()");
				continue;
			}

			// 멀티캐스트 그룹 탈퇴
			retval = setsockopt(chat_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
				(char*)&mreq, sizeof(mreq));
			if (retval == SOCKET_ERROR) err_quit("setsockopt()");

			break;
		}
	}


	closesocket(chat_sock);

	// 소켓 닫기
	printf("[TCP 서버] 클라이언트 종료: IP 주소=%s, 포트 번호=%d\r\n",
		addr, ntohs(clientaddr.sin_port));
	return 0;
}