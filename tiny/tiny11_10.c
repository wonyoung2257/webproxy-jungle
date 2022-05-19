#include "csapp.h"
#include <stdio.h>

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
// port 번호를 인자로 받아 클라이언트의 요청이 올 때마다 새로 연결 소켓을 만들어 doit() 함수를 호출한다.
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  // 클라이언트에서 연결 요청을 보내면 알 수 있는 클라이언트 연결 소켓 주소
  struct sockaddr_storage clientaddr;
  // Check command line args
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 해당 포트 번호에 해당하는 듣기 소켓 식별자를 열어준다.
  listenfd = Open_listenfd(argv[1]);
  // 클라이언트의 요청이 올 때마다 새로 연결 소켓을 만들어 doit() 호출
  while (1) {
    // 클라이언트에게서 받은 연결 요청을 accept한다. connfd = 서버 연결 식별자
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept

    // 연결이 성공했다는 메세지를 위해. Getnameinfo를 호출하면서 hostname과 port가 채워진다.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // doit 함수를 실행!
    doit(connfd);   // line:netp:tiny:doit
    // 서버 연결 식별자를 닫아준다.
    Close(connfd);  // line:netp:tiny:close
  }
}
// doit func은 HTTP transaction을 다룬다.
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  // 클라이언트에게서 받은 요청(rio)으로 채워진다.
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  // parse_uri를 통해 채워진다.
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  // 1. Read request line and headers
  // 클라이언트가 rio로 보낸 request 라인과 헤더를 읽고 분석한다.
  Rio_readinitb(&rio, fd);  // rio 버퍼와 fd, 여기서는 서버의 connfd를 연결시켜준다.
  Rio_readlineb(&rio, buf, MAXLINE);  // 그리고 rio(==connfd)에 있는 string 한 줄(응답 라인)을 모두 buf로 옮긴다.
  printf("Requesst headers:\n");
  printf("%s", buf);  // 요청 라인 buf = "GET /godzilla.gif HTTP/1.1\0"을 표준 출력만 해줌!
  sscanf(buf, "%s %s %s", method, uri, version);  // buf에서 문자열 3개를 읽어와 method, uri, version이라는 문자열에 저장한다.
  // 요청 method가 GET이 아니면 종료. main으로 가서 연결 닫고 다음 요청 기다림.(TINY는 GET method만 제공. 만약 POST 요청하면 에러 메시지를 반환.)
  if  (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  // 요청 라인을 뺀 나머지 요청 헤더들을 무시(그냥 프린트)한다.
  read_requesthdrs(&rio);
  // 2. Parse URI from GET request
  // parse_uri : 클라이언트 요청 라인에서 받아온 uri를 이용해 정적/동적 컨텐츠를 구분한다.
  is_static = parse_uri(uri, filename, cgiargs);  // 정적이면 1

  // stat(file, *buffer) : file의 상태를 buffer에 넘긴다.
  // 여기서 filename : 클라이언트가 요청한 서버의 컨텐츠 디렉토리 및 파일 이름?
  if (stat(filename, &sbuf) < 0) {  // 못 넘기면 fail. 파일이 없다. 404.
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  // 3. distinguish the request is static or dynamic - 컨텐츠의 유형(정적, 동적)을 파악한 후 각각의 서버에 보낸다.
  if (is_static) { /* Serve static content */
    // !(일반 파일이다) or !(읽기 권한이 있다)
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny could't read the file");
      return;
    }
    // 정적 서버에 파일의 사이즈를 같이 보낸다. -> Response header에 Content-length 위해!
    serve_static(fd, filename, sbuf.st_size);
  }
  else { /* Serve dynamic content */
    // !(일반 파일이다) or !(실행 권한이 있다)
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    // 동적 서버에 인자를 같이 보낸다.
    serve_dynamic(fd, filename, cgiargs);
  }
}
// 에러 메세지와 응답 본체를 서버 소켓을 통해 클라이언트에 보낸다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  // Build the HTTP response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  // Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  // 에러메세지와 응답 본체를 서버 소켓을 통해 클라이언트에 보낸다.
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
// 클라이언트가 버퍼 rp에 보낸 나머지 요청 헤더들을 무시한다(그냥 프린트한다).
// 요청부분을 제외한 나머지 메시지가 출력된다.
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  // 버퍼 rp의 마지막 끝을 만날 때까지("Content-length: %d\r\n\r\n에서 마지막 \r\n)
  // 계속 출력해줘서 없앤다.
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
/* TINY assumes that the hone directory for static content is its current directory and that the home directory for executables is ./cgi-bin.
Any URI that contains the string cgi-bun is assumed to denote a request for dynamic content. The default filename is ./home.html. */
// uri를 받아 요청받은 파일의 이름(filename)과 요청 인자(cgiarg)를 채워준다.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;
  // uri에 cgi-bin이 없다면, 즉 정적 컨텐츠를 요청한다면 1을 리턴한다.
  // 예시 : GET /godzilla.jpg HTTP/1.1 -> uri에 cgi-bin이 없다
  if (!strstr(uri, "cgi-bin")) {  // Static content, uri안에 "cgi-bin"과 일치하는 문자열이 있는지
    strcpy(cgiargs, "");  // 정적이니까 cgiargs는 아무것도 없다
    strcpy(filename, ".");  // 현재경로에서부터 시작 ./path ~~
    strcat(filename, uri);  // filename 스트링에 uri 스트링을 이어붙인다.
    write(1, "check", sizeof("check")); // 표준 입출력 1번에 check 문자열 삽입
    // 만약 uri뒤에 '/'이 있다면 그 뒤에 home.html을 붙인다.
    // 내가 브라우저에 http://localhost:5000만 입력하면 바로 뒤에 '/'이 생기는데,
    // '/' 뒤에 home.html을 붙여 해당 위치 해당 이름의 정적 컨텐츠가 출력된다.
    if (uri[strlen(uri)-1] == '/') //만약 URI가 /로 끝나면: => 여기서 문자 하나에 큰 따옴표 붙이면 NULL로 인식해서 에러!
    {
      strcat(filename, "home.html"); // default filename인 home을 붙인다.
    }
    /*
      예시
      uri : /godzilla.jpg
      ->
      cgiargs :
      filename : ./godzilla.jpg
    */
    // 정적 컨텐츠면 1 리턴
    return 1;
  }
  else { // request가 dynamic content에 대한 요청일 때: CGI argument를 추출한 다음 남아있는 URI 부분을 relative Linux filename으로 바꾼다.
    ptr = index(uri, '?');
    // printf("ptr: %c\n", *ptr);
    
    // '?'가 있으면 cgiargs를 '?' 뒤 인자들과 값으로 채워주고 ?를 NULL로 만든다.
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    // '?'가 없으면 그냥 아무것도 안 넣어준다.
    else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");  // 현재 디렉토리에서 시작
    strcat(filename, uri);  // uri 넣어준다.

    /*
      예시
      uri : /cgi-bin/adder?123&123
      ->
      cgiargs : 123&123
      filename : ./cgi-bin/adder
    */
    return 0;
  }
}
// 클라이언트가 원하는 정적 컨텐츠 디렉토리를 받아온다. 응답 라인과 헤더를 작성하고 서버에게 보낸다.
// 그 후 정적 컨텐츠 파일을 읽어 그 응답 본체를 클라이언트에 보낸다
// serve_static 함수는 HTTP response를 전송 => 로컬 파일의 콘텐츠를 바디에 실어서
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  // Send response headers to client 클라이언트에게 응답 헤더 보내기
  // 응답 라인과 헤더 작성
  get_filetype(filename, filetype);  // 파일 타입 찾아오기
  sprintf(buf, "HTTP/1.0 200 OK\r\n");  // 응답 라인 작성 buf에 담긴다.
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);  // 응답 헤더 작성
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  // 응답 라인과 헤더를 클라이언트에게 보냄
  Rio_writen(fd, buf, strlen(buf));  // connfd를 통해 clientfd에게 보냄
  printf("Response headers:\n");
  printf("%s", buf);  // 서버 측에서도 출력한다.

  /* Send response body to client */
  // 문제 11.9
  srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
  
  srcp = (char*)Malloc(filesize);  // 파일 크기만큼의 메모리를 동적할당한다.
  Rio_readn(srcfd, srcp, filesize);   // filename 내용을 동적할당한 메모리에 쓴다.
  Close(srcfd); // 파일을 닫는다.
  Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 클라이언트에 보낸다(읽는다).
  free(srcp);
  
  // 기존 서버
  // srcfd = Open(filename, O_RDONLY, 0); // filename의 이름을 갖는 파일을 읽기 권한으로 불러온다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 메모리에 파일 내용을 동적할당한다.
  // Close(srcfd); // 파일을 닫는다.
  // Rio_writen(fd, srcp, filesize);  // 해당 메모리에 있는 파일 내용들을 fd에 보낸다(읽는다).
  // Munmap(srcp, filesize);
}
// filename을 조사해 각각의 식별자에 맞는 MIME 타입을 filetype에 입력해준다.
// Response header의 Content-type에 필요하다.
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpg");
  else
    strcpy(filetype, "text/plain");
}
// 클라이언트가 원하는 동적 컨텐츠 디렉토리를 받아온다.
//응답 라인과 헤더를 작성하고 서버에게 보낸다.
// CGI 자식 프로세스를 fork하고 그 프로세스의 표준 출력을 클라이언트 출력과 연결한다.
void serve_dynamic(int fd, char*filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = { NULL };
  
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  
  if (Fork() == 0) { /* Child */
    /* Real server would set all CGI vars here */

    setenv("QUERY_STRING", cgiargs, 1);  //
    // 클라이언트의 표준 출력을 CGI 프로그램의 표준 출력과 연결한다.
    // 이제 CGI 프로그램에서 printf하면 클라이언트에서 출력됨
    // STDOUT_FILENO(1)은 모니터에 출력을 한다.
    // 이 STDOUT_FILENO(1)을 fd(4 클라이언트)로 출력을 바꿔 클라이언트에 출력하게 한다.
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */
    // 여기부터 출력되는 값은 클라이언트에 출력되고 telnet으로 확인할 수 있다.
    Execve(filename, emptylist, environ); /* Run CGI program */
  }
  Wait(NULL); /* Parent waits for and reaps child */
}