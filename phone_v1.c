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

#define BUFSIZE 256   // byte

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

void start_rec(fd_set *rfds, int *max_fd, pid_t *pid, int *pfd) {
  char *args[] = {"rec", "-traw",  "-b16",  "-c1",  "-es",  "-r8000", "-q", "-", "--buffer", "256", NULL};
  create_process("rec", args, pid, pfd);

  FD_SET(*pfd, rfds);
  if (*max_fd < *pfd) *max_fd = *pfd;
}

int main(int argc, char **argv){
  if (argc != 2) die("option");

  struct timeval st, en;
  // gettimeofday(&st, NULL);
  //gettimeofday(&en, NULL);
  //fprintf(stderr, "%.2f\n", (en.tv_sec-st.tv_sec)+(en.tv_usec-st.tv_usec)*1E-6);

  int s = socket(PF_INET, SOCK_DGRAM, 0);
  
  struct sockaddr_in my_addr;
  socklen_t my_addr_len = sizeof(my_addr);
  my_addr.sin_family = AF_INET;
  my_addr.sin_addr.s_addr = INADDR_ANY;
  my_addr.sin_port = htons(atoi(argv[1]));

  struct sockaddr_in ot_addr;
  socklen_t ot_addr_len;
  ot_addr.sin_family = AF_INET;

  /* 待ち受け番号設定 */
  if (bind(s, (struct sockaddr *)&my_addr, my_addr_len) != 0) {
    die("bind");
  }

  pid_t rec_pid;
  int rec_pfd = -1;
  
  short sbuf[BUFSIZE/2];
  char cbuf[BUFSIZE/2];
  
  fd_set rfds, rfds_cp; // monitor socket, stdin, pipe (rec)
  FD_ZERO(&rfds_cp);
  FD_SET(s, &rfds_cp);
  FD_SET(0, &rfds_cp);
  
  int recording = 0;   // flag to tell if recording already started
  int max_fd = s;     // max file descriptor (used by select())

  while(1) {
    memcpy(&rfds, &rfds_cp, sizeof(fd_set));
    select(max_fd + 1, &rfds, NULL, NULL, NULL);
    
    if (FD_ISSET(s, &rfds)) {  // socket readable
      ot_addr_len = sizeof(struct sockaddr_in);
      int m = recvfrom(s, sbuf, BUFSIZE, 0, (struct sockaddr *)&ot_addr, &ot_addr_len);
      if (m == -1) die("recv");
      write(1, sbuf, m);
      if (!recording) {
        start_rec(&rfds_cp, &max_fd, &rec_pid, &rec_pfd);
        recording = 1;
      }
    }

    if (recording && FD_ISSET(rec_pfd, &rfds)) {     // pipe (rec command) readable */
      int n = read(rec_pfd, sbuf, BUFSIZE);
      int m_send = sendto(s, sbuf, n, 0, (struct sockaddr *)&ot_addr, ot_addr_len);
      if (m_send == -1) die("send");
    }

    if (FD_ISSET(0, &rfds)) {  // stdin readable
      fgets(cbuf, BUFSIZE, stdin);
      
      if (strcmp(cbuf, "quit\n") == 0) {
        if (recording) {
          if (kill(rec_pid, SIGKILL) == -1) die("kill");
          wait(NULL);
        }
        break;
      }

      if (strncmp(cbuf, "call", 4) == 0) {
        char *str = strtok(cbuf, " ");  // "call"
        str = strtok(NULL, " ");        // ip address
        if (str == NULL) die("argument");
        ot_addr.sin_addr.s_addr = inet_addr(str);
        str = strtok(NULL, " ");        // port
        if (str == NULL) die("argument");
        str[strlen(str) - 1] = '\0';    // delete LF
        ot_addr.sin_port = htons(atoi(str));
        ot_addr_len = sizeof(ot_addr);

        recording = 1;
        start_rec(&rfds_cp, &max_fd, &rec_pid, &rec_pfd);
      }
    }
  }

  close(s);

  return 0;
}
  
