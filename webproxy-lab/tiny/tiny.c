/***************************************************************
 * 프로그램 전체 흐름(순서 요약)
 *
 * A. 서버 시작(main)
 *  1) 포트 인자 확인
 *  2) 리슨 소켓 열기(Open_listenfd)
 *  3) 무한 루프
 *     3-1) 클라이언트 연결 수락(Accept)
 *     3-2) 접속 정보 출력(Getnameinfo)
 *     3-3) 한 요청 처리(doit)
 *     3-4) 연결 닫기(Close)
 *
 * B. 한 요청 처리(doit)
 *  1) 요청 라인 읽기(GET /path HTTP/1.0)
 *  2) GET 이외의 메서드면 501 오류 응답
 *  3) 헤더 줄들 읽고(이 예제는) 무시
 *  4) URI 해석(parse_uri) → 정적/동적 판별, 파일 경로/인자 얻기
 *  5) 파일 존재 확인(stat)
 *  6) 정적이면 serve_static, 동적이면 serve_dynamic 호출
 *
 * C. 정적 파일 전송(serve_static)
 *  1) 파일 타입 결정(get_filetype)
 *  2) 상태줄/헤더 전송
 *  3) 파일 내용을 읽어 본문으로 전송(Mmap + Rio_writen)
 *
 * D. 동적 콘텐츠 전송(serve_dynamic)
 *  1) 200 OK 헤더 전송
 *  2) 자식 프로세스 fork
 *     - 환경변수 QUERY_STRING 설정
 *     - 표준출력을 소켓으로 바꾸고(dup2)
 *     - CGI 프로그램 실행(execve)
 *  3) 부모는 자식 종료까지 대기(wait)
 *
 * E. 오류 응답(clienterror)
 *  1) 간단한 HTML 본문 만들기
 *  2) 상태줄/헤더/본문 순서로 전송
 ***************************************************************/

/* $begin tinymain */                      // 교재 마크업 주석(그대로 둠)
/*
 * tiny.c - 간단한 반복형 HTTP/1.0 웹서버
 *   - GET 요청만 처리
 *   - 정적/동적 콘텐츠 모두 지원
 *
 * Updated 11/2019 droh
 *   - serve_static(), clienterror()의 sprintf 사용 개선
 */
#include "csapp.h"                          // 안전 I/O(RIO), 소켓 래퍼 등 유틸 모음

/* 함수 선언부 */                           // 아래에서 정의할 함수들의 선언
void doit(int fd);                          // 한 클라이언트 요청 처리
void read_requesthdrs(rio_t *rp);           // 요청 헤더들 읽기
int parse_uri(char *uri, char *filename, char *cgiargs); // URI 해석
void serve_static(int fd, char *filename, int filesize); // 정적 전송
void get_filetype(char *filename, char *filetype);       // 파일 확장자 → MIME
void serve_dynamic(int fd, char *filename, char *cgiargs);// 동적 전송(CGI)
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);            // 오류 응답

/* main - 기본 서버 루프 */
int main(int argc, char **argv)             // 프로그램 시작
{
    int listenfd, connfd;                   // 대기소켓, 연결소켓
    char hostname[MAXLINE], port[MAXLINE];  // 접속한 쪽의 호스트/포트 문자열
    socklen_t clientlen;                    // 주소 길이
    struct sockaddr_storage clientaddr;     // 클라이언트 주소 저장용(IPv4/6 모두 지원)

    /* 명령행 인자 확인 */
    if (argc != 2)                          // 포트 번호 인자가 없으면
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 안내
        exit(1);                            // 종료
    }

    /* 서버 소켓 열기 */
    listenfd = Open_listenfd(argv[1]);      // 주어진 포트로 리슨 소켓 생성
    while (1)                               // 무한히 요청을 받음(반복 서버)
    {
        clientlen = sizeof(clientaddr);     // 주소 버퍼 크기 세팅
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 클라이언트 연결 수락
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
                                            // 접속한 쪽의 호스트/포트 문자열 얻기
        printf("Accepted connection from (%s, %s)\n", hostname, port); // 접속 로그
        doit(connfd);                       // 한 요청 처리
        Close(connfd);                      // 처리 끝났으니 연결 닫기
    }
}
/* $end tinymain */                         // 교재 마크업 주석(그대로 둠)

/* doit - 한 번의 HTTP 트랜잭션 처리 */
void doit(int fd)                           // fd: 이 클라이언트와 통신할 소켓
{
    int is_static;                          // 정적(1)/동적(0) 구분 플래그
    struct stat sbuf;                       // 파일 상태 정보(stat 결과)
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청 라인 파싱용
    char filename[MAXLINE], cgiargs[MAXLINE]; // 실제 파일경로, CGI 인자
    rio_t rio;                              // 버퍼링된 입력용 RIO 구조체

    /* 요청 라인과 헤더 읽기 */
    Rio_readinitb(&rio, fd);                // RIO 초기화(소켓 fd에 연결)
    Rio_readlineb(&rio, buf, MAXLINE);      // 첫 줄(요청 라인) 읽기
    printf("Request headers:\n");           // 디버깅용 출력
    printf("%s", buf);                      // 요청 라인 그대로 출력
    sscanf(buf, "%s %s %s", method, uri, version); // "GET /path HTTP/1.1" 파싱

    if (strcasecmp(method, "GET"))          // GET 이 아니면
    {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method"); // 501 응답
        return;                             // 처리 종료
    }

    read_requesthdrs(&rio);                 // 헤더 줄들 읽기(이 예제는 내용 무시)

    /* URI 분석 */
    is_static = parse_uri(uri, filename, cgiargs); // 정적/동적 판별 + 경로/인자 추출
    if (stat(filename, &sbuf) < 0)          // 파일/프로그램 존재 여부 확인
    {
        clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn’t find this file"); // 404 응답
        return;                             // 처리 종료
    }

    if (is_static)                          // 정적 파일이면
    { /* 정적 콘텐츠 */
        serve_static(fd, filename, sbuf.st_size); // 파일 크기 넘겨 전송
    }
    else                                     // 동적이면(CGI)
    { /* 동적 콘텐츠 */
        serve_dynamic(fd, filename, cgiargs);    // 실행 결과를 전송
    }
}

/* read_requesthdrs - 요청 헤더 읽기(내용은 버림) */
void read_requesthdrs(rio_t *rp)            // rp: RIO 입력 상태
{
    char buf[MAXLINE];                      // 한 줄 버퍼
    Rio_readlineb(rp, buf, MAXLINE);        // 첫 헤더 줄 읽기
    while (strcmp(buf, "\r\n"))             // 빈 줄 나올 때까지(헤더 끝 표시)
    {
        Rio_readlineb(rp, buf, MAXLINE);    // 다음 헤더 줄 읽기
        printf("%s", buf);                  // 디버깅용 출력
    }
    return;                                 // 헤더 처리 끝
}

/* parse_uri - URI를 파일경로와 CGI 인자로 분리 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
    char *ptr;                              // '?' 위치 찾기용 포인터

    if (!strstr(uri, "cgi-bin"))            // "cgi-bin"이 없으면 정적
    { /* 정적 콘텐츠 */
        strcpy(cgiargs, "");                // CGI 인자 없음
        strcpy(filename, ".");              // 현재 디렉터리를 기준으로
        strcat(filename, uri);              // 요청 경로를 붙임
        if (uri[strlen(uri) - 1] == '/')    // 디렉터리로 끝나면
            strcat(filename, "home.html");  // 기본 파일로 home.html 사용
        return 1;                           // 정적 표시
    }
    else                                    // "cgi-bin" 포함 → 동적
    { /* 동적 콘텐츠 */
        ptr = index(uri, '?');              // ? 뒤가 쿼리 문자열
        if (ptr)                            // ?가 있으면
        {
            strcpy(cgiargs, ptr + 1);       // ? 다음부터를 인자로 저장
            *ptr = '\0';                    // URI 부분만 남기도록 문자열 자름
        }
        else
            strcpy(cgiargs, "");            // 인자 없으면 빈 문자열
        strcpy(filename, ".");              // 현재 디렉터리 기준
        strcat(filename, uri);              // 실행할 CGI 경로 구성
        return 0;                           // 동적 표시
    }
}

/* serve_static - 정적 파일 전송 */
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;                              // 읽을 파일 디스크립터
    char *srcp, filetype[MAXLINE], buf[MAXBUF]; // 메모리 매핑 포인터, MIME, 헤더 버퍼

    /* 응답 헤더 작성 */
    get_filetype(filename, filetype);       // 확장자로 MIME 결정
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    // 상태줄
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);         // 서버 이름
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);    // 본문 길이
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);  // 콘텐츠 타입 + 빈 줄
    Rio_writen(fd, buf, strlen(buf));       // 헤더 전송

    /* 응답 본문(파일) 전송 */
    srcfd = Open(filename, O_RDONLY, 0);    // 파일 열기(읽기 전용)
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 파일을 메모리에 매핑
    Close(srcfd);                           // 파일 디스크립터는 닫아도 됨(매핑 유지)
    Rio_writen(fd, srcp, filesize);         // 매핑된 내용을 소켓으로 보냄
    Munmap(srcp, filesize);                 // 매핑 해제
}

/* get_filetype - 확장자에서 MIME 타입 결정 */
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))          // .html
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))      // .gif
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg"))      // .jpg
        strcpy(filetype, "image/jpeg");
    else                                    // 기본값
        strcpy(filetype, "text/plain");
}

/* serve_dynamic - CGI 프로그램 실행 결과 전송 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = {NULL}; // execve 인자 리스트(빈 배열)

    /* 응답 헤더 전송(본문은 CGI가 출력) */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");   // 상태줄
    Rio_writen(fd, buf, strlen(buf));     // 전송
    sprintf(buf, "Server: Tiny Web Server\r\n"); // 서버 이름
    Rio_writen(fd, buf, strlen(buf));     // 전송

    if (Fork() == 0)                      // 자식 프로세스 분기
    { /* 자식 프로세스 */
        setenv("QUERY_STRING", cgiargs, 1); // CGI가 읽을 쿼리 문자열 설정
        Dup2(fd, STDOUT_FILENO);            // 표준출력을 소켓으로 변경
        Execve(filename, emptylist, environ);// CGI 실행(출력은 소켓으로 감)
    }
    Wait(NULL);                            // 부모는 자식 종료를 기다림
}

/* clienterror - 오류 페이지 전송 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];       // 헤더/본문 버퍼

    /* 응답 본문(간단 HTML) 만들기 */
    sprintf(body, "<html><title>Tiny Error</title>");               // 제목
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);          // 배경색 등
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);          // 상태 코드/메시지
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);         // 상세 이유
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);  // 꼬리말

    /* 상태줄/헤더/본문 전송 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 상태줄
    Rio_writen(fd, buf, strlen(buf));                     // 전송
    sprintf(buf, "Content-type: text/html\r\n");          // 타입
    Rio_writen(fd, buf, strlen(buf));                     // 전송
    sprintf(buf, "Content-length: %lu\r\n\r\n", strlen(body)); // 길이 + 빈 줄
    Rio_writen(fd, buf, strlen(buf));                     // 전송
    Rio_writen(fd, body, strlen(body));                   // 본문 전송
}