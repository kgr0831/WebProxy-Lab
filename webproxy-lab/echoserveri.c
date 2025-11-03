/***************************************************************
 * 프로그램 흐름도 (간단한 순서 요약)
 *
 * 1. 프로그램 시작 → 포트 번호 입력 확인
 * 2. 지정된 포트로 서버 소켓 생성 (Open_listenfd)
 * 3. 무한 반복:
 *      3-1. 클라이언트 연결 대기 (Accept)
 *      3-2. 클라이언트 주소/IP 확인 (Getnameinfo)
 *      3-3. echo(connfd) 호출 → 데이터 주고받기
 *      3-4. 연결 종료 (Close)
 * 4. 다음 클라이언트 연결 대기
 ***************************************************************/

#include "csapp.h"  // 네트워크 함수와 입출력 유틸 포함

/* ---------- echo() : 클라이언트와 실제 데이터 주고받는 부분 ---------- */
void echo(int connfd) {            // connfd: 클라이언트와 연결된 통신 번호
    size_t n;                      // 읽은 바이트 수 저장용
    char buf[MAXLINE];             // 받은 데이터를 담을 버퍼
    rio_t rio;                     // 버퍼링된 입출력 관리 구조체

    Rio_readinitb(&rio, connfd);   // RIO 초기화 (소켓을 버퍼와 연결)
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {  // 한 줄씩 읽기
        printf("server received %d bytes\n", (int)n);       // 읽은 바이트 수 표시
        Rio_writen(connfd, buf, n);                         // 받은 내용 그대로 돌려보내기
    }
    // 클라이언트가 연결을 끊으면 0 반환 → 루프 종료
}

/* ---------- main() : 서버의 기본 동작 흐름 ---------- */
int main(int argc, char **argv) {
    int listenfd, connfd;            // listenfd: 대기용, connfd: 실제 연결용
    socklen_t clientlen;             // 클라이언트 주소 크기 저장용
    struct sockaddr_storage clientaddr;  // 클라이언트 주소 정보 저장
    char client_hostname[MAXLINE], client_port[MAXLINE];  // IP/포트 문자열 저장

    /* 1. 실행 인자 확인 (포트 번호 필수 입력) */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);  // 잘못된 사용법 출력
        exit(0);                                         // 프로그램 종료
    }

    /* 2. 서버용 소켓 열기 (전화선 개통) */
    listenfd = Open_listenfd(argv[1]);   // 지정된 포트로 서버 소켓 생성

    /* 3. 무한 루프: 계속해서 클라이언트 연결 받기 */
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);           // 주소 크기 설정
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 접속 대기

        /* 3-1. 접속한 클라이언트의 IP와 포트 가져오기 */
        Getnameinfo((SA *)&clientaddr, clientlen,
                    client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        /* 3-2. 클라이언트와 데이터 송수신 */
        echo(connfd);        // 데이터 읽고 그대로 돌려보냄

        /* 3-3. 연결 종료 */
        Close(connfd);       // 현재 연결 종료
    }
}
