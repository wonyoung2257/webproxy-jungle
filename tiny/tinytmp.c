/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// 하나의 HTTP 트랜잭션을 처리한다.
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // Read request line and headers
  // 요청 라인을 읽고 분석함
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE); // 이 함수를 사용하여 요청라인을 읽음
  printf("Requset headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  // GET요청이 아니면 에러메시지를 보내고 main루틴으로 돌아오고 그 후에 연결을 닫고 다음 연결 요청을 기다린다.
  if(strcasecmp(method, "GET")){
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // Rarse URI from GET reques
  // 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정함
  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny does couldn't find this file");
    return;
  }
  if(is_static){ // Serve static content
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }else{ // Serve dynamic content
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return ;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}
// 서버 에러를 클라이언트에게 적절한 상태 코드와 상태메시지를 함께 보낸다.
// HTML파일도 함께 보냄
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  // Build the HTTP response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Wer server</em>\r\n", body);

  // Print thet HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 요청헤드를 읽고 무시???????
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// 
int parse_uri(char *uri, char *filename, char*cgiargs)
{
  char *ptr;

  if(!strstr(uri, "cgi-bin")){ // 정적 컨텐츠
    strcpy(cgiargs, ""); // 인자 스트링을 지움
    //URI를 상대리눅스 경로 이름으로 변환
    strcpy(filename, "."); 
    strcat(filename, uri);
    // '/'로 끝나면 기본 파일이름 추가
    if(uri[strlen(uri) -1] == '/')
    {
      strcat(filename, "home.html");
    }
    return 1;
  }
  else{ // 동적 컨텐츠
    ptr = index(uri, '?');
    if(ptr){
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  } 
}

// Tiny 서버는 다섯개의 서로 다른 정적 컨텐츠 타입 지원
// HTML 파일, 무형식 텍스트 파일, GIF, PNG, JPEG로 인코딩된 영상
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // Send responce headers to client
  get_filetype(filename, filetype); //파일 이름의 접미어를 검사해서 파일 타입을 결정한다.
  // 클라이언트에게 응답줄과 응답헤더를 보냄
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  // send response body to client
  // filename을 오픈하고 식별자를 언더옴
  srcfd = Open(filename, O_RDONLY, 0);
  // Mmap함수는 요쳥한 파일을 가상메모리 영역으로 맵핑
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // 더이상 식별자가 필요없어 파일을 닫음
  Close(srcfd);
  // 실제로 파일을 클라이언트에게 전송
  Rio_writen(fd, srcp, filesize);
  // 맵핑한 가상메모리 주소를 반환한다. 메모리 누수를 피하는데 중요함!
  Munmap(srcp, filesize);
}

void get_filetype(char *filename, char*filetype)
{
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  // Return first part of HTTP response
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if(Fork() == 0 ){ // child
    // QUERY_STRING 변수를 URI와 CGI인자들로 초기화한다.
    setenv("QUERY_STRING", cgiargs, 1);
    // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정함
    // Duplicating FD는 파일 디스크립트를 복사하는 함수이다.
    // fd -> STDOUT_FINENO으로 복사한다
    Dup2(fd, STDOUT_FILENO);
    // CGI 프로그램 로드 후 실행
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}

int main(int argc, char **argv) {

  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  printf("argc: %d\n", argc);
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 해당 포트 번호에 해당하는 듣기 소켓 식별자를 열어준다. fd table 3번에 들어감 */
  listenfd = Open_listenfd(argv[1]);

  while (1) { // 반복적인 연결 요청을 접수
    /* 클라이언트에게서 받은 연결 요청을 accept한다. connfd = 서버 연결 식별자 */
    clientlen = sizeof(clientaddr);
    printf("clientlen: %d\n", clientlen);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    //트랜잭션 수행
    doit(connfd);   // line:netp:tiny:doit
    //자신 쪽의 연결 끝을 닫는다.
    Close(connfd);  // line:netp:tiny:close
  }
}
