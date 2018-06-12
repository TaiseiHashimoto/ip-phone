#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#define BUFSIZE 128   // byte

void die(char *message) {
  perror(message);
  exit(1);
}

void create_process(char *cmd, char **args, pid_t *pid, int *pfd) {
  int pipefd[2];
  if (pipe(pipefd) == -1) die("pipe");

  *pid = fork();
  if (*pid < 0) die("fork");

  if (*pid == 0) {
    close(pipefd[0]);

    dup2(pipefd[1], 1);
    close(pipefd[1]);

    if (execvp(cmd, args) == -1) die("exec"); // 制御が戻るのはエラー発生時のみ
  } else {
    close(pipefd[1]);
  }

  *pfd = pipefd[0]; // 親プロセスは読み込み用パイプを受け取る
}

int main(int argc, char **argv){
  if (argc != 2) die("option");

  struct timeval st, en;
  gettimeofday(&st, NULL);

  int s = socket(PF_INET, SOCK_DGRAM, 0);
  
  struct sockaddr_in my_addr;
  socklen_t my_addr_len = sizeof(my_addr);
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(atoi(argv[1]));

  struct sockaddr_in ot_addr;
  socklen_t ot_addr_len;
  ot_addr.sin_family = AF_INET;

  
  // already in useを回避
  //const int one = 1;
  //setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

  /* 待ち受け番号設定 */
  if (bind(s, (struct sockaddr *)&my_addr, my_addr_len) != 0) {
    die("bind");
  }

  char *cmd = "rec", *args[] = {"rec", "-traw",  "-b16",  "-c1",  "-es",  "-r8000", "-q", "-", "--buffer", "256", NULL};
  pid_t pid;
  int pfd = -1;
  
  short sbuf[BUFSIZE/2];
  char cbuf[BUFSIZE/2];
  
  fd_set rfds, rfds_cp; // monitor socket, stdin, pipe (rec)
  FD_ZERO(&rfds_cp);
  FD_SET(s, &rfds_cp);
  FD_SET(0, &rfds_cp);
  
  int recieved = 0;   // have already recieved ? (flag)
  int max_fd = s;

  //gettimeofday(&en, NULL);
  //fprintf(stderr, "%.2f\n", (en.tv_sec-st.tv_sec)+(en.tv_usec-st.tv_usec)*1E-6);
  
  while(1) {
    memcpy(&rfds, &rfds_cp, sizeof(fd_set));
    select(max_fd + 1, &rfds, NULL, NULL, NULL);
    
    if (FD_ISSET(s, &rfds)) {  // socket readable
      ot_addr_len = sizeof(struct sockaddr_in);
      int m = recvfrom(s, sbuf, BUFSIZE, 0, (struct sockaddr *)&ot_addr, &ot_addr_len);
      if (m == -1) die("recv");
      write(1, sbuf, m);
      if (!recieved) {
        create_process(cmd, args, &pid, &pfd);  // start rec
        FD_SET(pfd, &rfds_cp);
        if (max_fd < pfd) max_fd = pfd;
        recieved = 1;
      }
    }

    if (FD_ISSET(0, &rfds)) {  // stdin readable
      fgets(cbuf, BUFSIZE, stdin);
      
      if (strcmp(cbuf, "quit\n") == 0) {
        if (kill(pid, SIGKILL) == -1) die("kill");
        wait(NULL);
        break;
      }

      if (strncmp(cbuf, "call", 4) == 0) {
        char *str = strtok(cbuf, " ");  // "call"
        str = strtok(NULL, " ");        // ip address
        if (str == NULL) die("argument");
        //fprintf(stderr, "ip = %s\n", str);
        ot_addr.sin_addr.s_addr = inet_addr(str);
        str = strtok(NULL, " ");        // port
        if (str == NULL) die("argument");
        str[strlen(str) - 1] = '\0';    // delete LF
        //fprintf(stderr, "port = %s\n", str);
        ot_addr.sin_port = htons(atoi(str));
        ot_addr_len = sizeof(ot_addr);

        recieved = 1;
        create_process(cmd, args, &pid, &pfd);  // start rec
        FD_SET(pfd, &rfds_cp);
        if (max_fd < pfd) max_fd = pfd;
      }
    }

    if (FD_ISSET(pfd, &rfds)) {     // pipe (rec command) readable */
      int n = read(pfd, sbuf, BUFSIZE);
      int m_send = sendto(s, sbuf, n, 0, (struct sockaddr *)&ot_addr, ot_addr_len);
      if (m_send == -1) die("send");

    }
  }

  close(s);

  return 0;
}
  
