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
#include <sys/ioctl.h>
#include <errno.h>

#define SOUND_BUF 256  // byte
#define CHAR_BUF 100
#define QUE_LEN 10

#define NO_SESSION  0
#define NEGOTIATING 1
#define INVITING    2
#define RINGING     3
#define SPEAKING    4

void die(char *message) {   // TODO: error handling (do not die)
  perror(message);
  exit(1);
}

int max(int a, int b) {
  return a > b ? a : b;
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

    if (execvp(cmd, args) == -1) die("exec");
  } else {
    close(pipefd[1]);
  }

  *pfd = pipefd[0];  // parent process receive READ pipe
}

void start_rec(fd_set *rfds, int *max_fd, pid_t *pid, int *pfd) {
  char *args[] = {"rec", "-traw",  "-b16",  "-c1",  "-es",  "-r8000", "-q", "-", "--buffer", "256", NULL};
  create_process("rec", args, pid, pfd);

  FD_SET(*pfd, rfds);
  *max_fd = max(*max_fd, *pfd);
}

int main(int argc, char **argv){
  if (argc != 3) die("argument");
  int my_tcp_port = atoi(argv[1]);
  int my_udp_port = atoi(argv[2]);

  int ret;

  // struct timeval st, en;
  // gettimeofday(&st, NULL);
  //gettimeofday(&en, NULL);
  //fprintf(stderr, "%.2f\n", (en.tv_sec-st.tv_sec)+(en.tv_usec-st.tv_usec)*1E-6);

  int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
  
  struct sockaddr_in my_tcp_addr, my_udp_addr;
  my_tcp_addr.sin_family = AF_INET;
  my_tcp_addr.sin_addr.s_addr = INADDR_ANY;
  my_tcp_addr.sin_port = htons(my_tcp_port);
  my_udp_addr.sin_family = AF_INET;
  my_udp_addr.sin_addr.s_addr = INADDR_ANY;
  my_udp_addr.sin_port = htons(my_udp_port);

  int yes = 1;   // avoid error "Address already in use"
  setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

  ret = bind(tcp_sock, (struct sockaddr *)&my_tcp_addr, sizeof(my_tcp_addr));
  if (ret == -1) die("bind (tcp)");
  ret = bind(udp_sock, (struct sockaddr *)&my_udp_addr, sizeof(my_udp_addr));
  if (ret == -1) die("bind (udp)");

  int nonblock = 1;
  ioctl(tcp_sock, FIONBIO, &nonblock);  // set sockets as non-blocking (use select())

  listen(tcp_sock, QUE_LEN);
  
  fd_set rfds, rfds_org; // monitor TCP/UDP sockets, stdin, rec pipe
  FD_ZERO(&rfds_org);
  FD_SET(tcp_sock, &rfds_org);
  FD_SET(udp_sock, &rfds_org);
  FD_SET(0, &rfds_org);
  
  int state = 0;   // flag to tell if talk has already started
  int max_fd = max(tcp_sock, udp_sock);     // max file descriptor (used by select())
  struct sockaddr_in ot_tcp_addr, ot_udp_addr;
  ot_tcp_addr.sin_family = AF_INET;
  ot_udp_addr.sin_family = AF_INET;
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);
  int tcp_s;
  pid_t rec_pid;
  int rec_pfd;
  short sbuf[SOUND_BUF/2];
  char cbuf[CHAR_BUF];

  while(1) {
    memcpy(&rfds, &rfds_org, sizeof(fd_set));
    select(max_fd + 1, &rfds, NULL, NULL, NULL);

    if (state == NO_SESSION) {
      if (FD_ISSET(tcp_sock, &rfds)) {  // TCP socket
        tcp_s = accept(tcp_sock, (struct sockaddr *)&ot_tcp_addr, &ot_addr_len);
        if (tcp_s == -1) {
          if (errno != EAGAIN) die("accept");
          // case EAGAIN : C connect => C cancel => S accept
        } else {
          state = NEGOTIATING;
          fprintf(stderr, "negotiating\n");

          // set IP address
          ot_tcp_addr.sin_addr.s_addr = ot_tcp_addr.sin_addr.s_addr;

          FD_SET(tcp_s, &rfds_org);
          max_fd = max(max_fd, tcp_s);
          continue;
        }
      }
      if (FD_ISSET(0, &rfds)) {  // stdin
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // delete LF
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "quit") == 0) {
          if (state == SPEAKING) {
            ret = kill(rec_pid, SIGKILL);
            if (ret == -1) die("kill");
            wait(NULL);
          }
          state = NO_SESSION;
          break;
        }
        else if (strcmp(str, "call") == 0) {
          // get ip address and port
          char *ip_addr = strtok(NULL, " ");
          if (ip_addr == NULL) die("argument (ip addr)");
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (tcp port)");
          int ot_tcp_port = atoi(str);
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (udp port)");
          int ot_udp_port = atoi(str);

          // address settings
          ret = inet_aton(ip_addr, &ot_tcp_addr.sin_addr);
          if (ret == 0) die("inet");
          ot_tcp_addr.sin_port = htons(ot_tcp_port);
          ret = inet_aton(ip_addr, &ot_udp_addr.sin_addr);
          if (ret == 0) die("inet");
          ot_udp_addr.sin_port = htons(ot_udp_port);

          // TCP connection
          tcp_s = socket(PF_INET, SOCK_STREAM, 0);
          ret = connect(tcp_s, (struct sockaddr *)&ot_tcp_addr, sizeof(ot_tcp_addr));
          if (ret == -1) die("connect");

          // send INVITE message
          sprintf(cbuf, "INVITE %d", my_udp_port);
          ret = send(tcp_s, cbuf, strlen(cbuf) + 1, 0);
          if (ret == -1) die("send");
          state = INVITING;
          fprintf(stderr, "inviting\n");

          FD_SET(tcp_s, &rfds_org);
          max_fd = max(max_fd, tcp_s);
          continue;
        }
      }
    }

    else if (state == NEGOTIATING) {  // established TCP connection
      if (FD_ISSET(tcp_s, &rfds)) {   // TCP socket
        ret = recv(tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
        if (ret == -1 && errno != EAGAIN) die("recv");
        char *str = strtok(cbuf, " ");
        if (strcmp(str, "INVITE") == 0) {
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (ot udp port)");
          ot_udp_addr.sin_port = htons(atoi(str));
          state = RINGING;
          fprintf(stderr, "answer ? ");
        }
      }
    }

    else if (state == INVITING) {   // waiting for answer
      if (FD_ISSET(tcp_s, &rfds)) {  // TCP socket
        ret = recv(tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
        if (ret == -1 && errno != EAGAIN) die("recv");
        if (strcmp(cbuf, "OK") == 0) {
          state = SPEAKING;
          start_rec(&rfds_org, &max_fd, &rec_pid, &rec_pfd);
          fprintf(stderr, "ok  speaking\n");
        }
      }
    }

    else if (state == RINGING) {   // being invited by the other
      if (FD_ISSET(0, &rfds)) {  // stdin
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // delete LF
        if (strcmp(cbuf, "yes") == 0) {
          strcpy(cbuf, "OK");
          ret = send(tcp_s, cbuf, strlen(cbuf) + 1, 0);
          // FD_SET(udp_sock, &rfds_org);
          if (ret == -1) die("send");
          state = SPEAKING;
          start_rec(&rfds_org, &max_fd, &rec_pid, &rec_pfd);
          fprintf(stderr, "speaking\n");
          continue;
        }
      }
    }

    else if (state == SPEAKING) {  // speaking
      if (FD_ISSET(udp_sock, &rfds)) {  // UDP socket
        ret = recvfrom(udp_sock, sbuf, SOUND_BUF, MSG_DONTWAIT, (struct sockaddr *)&ot_udp_addr, &ot_addr_len);
        if (ret == -1 && errno != EAGAIN) die("recv");
        write(1, sbuf, ret);
      }
      if (FD_ISSET(rec_pfd, &rfds)) {  // rec pipe
        ret = read(rec_pfd, sbuf, SOUND_BUF);
        ret = sendto(udp_sock, sbuf, ret, 0, (struct sockaddr *)&ot_udp_addr, sizeof(ot_udp_addr));
        if (ret == -1) die("send");
      }
    }
  }

  close(tcp_sock);
  close(udp_sock);
  close(tcp_s);

  return 0;
}
  
