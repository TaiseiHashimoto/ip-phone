#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "phone.h"


int main(int argc, char **argv) {
  int ret;

  if (argc != 3) die("argument", paNoError);  // 引数の個数チェック

  // アドレス構造体の設定
  InetData_t InetData;

  InetData.tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  InetData.udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
  // 自分のポート番号は引数から設定する
  InetData.my_tcp_port = atoi(argv[1]);  
  InetData.my_udp_port = atoi(argv[2]);
  InetData.my_tcp_addr.sin_family = AF_INET;
  InetData.my_tcp_addr.sin_addr.s_addr = INADDR_ANY;
  InetData.my_tcp_addr.sin_port = htons(InetData.my_tcp_port);
  InetData.my_udp_addr.sin_family = AF_INET;
  InetData.my_udp_addr.sin_addr.s_addr = INADDR_ANY;
  InetData.my_udp_addr.sin_port = htons(InetData.my_udp_port);
  InetData.ot_tcp_addr.sin_family = AF_INET;
  InetData.ot_udp_addr.sin_family = AF_INET;

  int reuse = 1;   // "Address already in use"のエラーを回避する
  setsockopt(InetData.tcp_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(int));
  int nonblock = 1;   // select()を使用するため、ソケットをノンブロッキングに設定
  ioctl(InetData.tcp_sock, FIONBIO, &nonblock);

  ret = bind(InetData.tcp_sock, (struct sockaddr *)&InetData.my_tcp_addr, sizeof(struct sockaddr_in));
  if (ret == -1) die("bind (tcp)", paNoError);
  ret = bind(InetData.udp_sock, (struct sockaddr *)&InetData.my_udp_addr, sizeof(struct sockaddr_in));
  if (ret == -1) die("bind (udp)", paNoError);

  listen(InetData.tcp_sock, QUE_LEN);

  enum Status status = NO_SESSION;

  // select()のための設定
  // TCP/UDPソケット、標準入力を監視する
  MonitorData_t MonitorData;
  FD_ZERO(&MonitorData.rfds_org);
  FD_SET(InetData.tcp_sock, &MonitorData.rfds_org);
  FD_SET(InetData.udp_sock, &MonitorData.rfds_org);
  FD_SET(STDIN_FILENO, &MonitorData.rfds_org);

  status = NO_SESSION;       // セッションのステータスを初期化

  // 監視するファイルディスクリプタの最大値(select()で使用)
  MonitorData.max_fd = max(InetData.tcp_sock, InetData.udp_sock);  
  char cbuf[CHAR_BUF];      // セッション管理用の文字列のバッファ

  PaStream *audioStream;

  RecAndSendData_t RecAndSendData;             // UDPでの送信用の情報
  RecAndSendData.sock = InetData.udp_sock;       // ソケットID
  RecAndSendData.addr = &InetData.ot_udp_addr;   // アドレス構造体のポインタ
  RecAndSendData.silent_frames = 0;     // 無音が連続しているフレーム数(0に初期化)

  while (1) {   // TODO: セッション未開始時もUDPで受信してしまう => 無限ループ
    memcpy(&MonitorData.rfds, &MonitorData.rfds_org, sizeof(fd_set));
    select(MonitorData.max_fd + 1, &MonitorData.rfds, NULL, NULL, NULL);

    if (status == NO_SESSION) {
      if (FD_ISSET(InetData.tcp_sock, &MonitorData.rfds)) {  // TCP socket (not accepted)
        ret = accept_connection(&InetData, &MonitorData);
        if (ret) {
          status = NEGOTIATING;
          fprintf(stderr, "negotiating\n");
          continue;
        }
      }
      if (FD_ISSET(STDIN_FILENO, &MonitorData.rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字を削除
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "call") == 0) {   // callコマンド
          ret = create_connection(&InetData, &MonitorData, str);
          if (ret) {
            status = INVITING;
            fprintf(stderr, "inviting\n");
            continue;
          }
        }
      }
    }

    else if (status == NEGOTIATING) {  // セッションの確立中の場合
      if (FD_ISSET(InetData.tcp_s, &MonitorData.rfds)) {   // TCPソケット
        ret = recv_invitation(&InetData, &MonitorData);
        if (ret) {
          status = RINGING;
          fprintf(stderr, "answer ? ");
          continue;
        }
      }
    }

    else if (status == INVITING) {   // 呼び出し中の場合
      if (FD_ISSET(InetData.tcp_s, &MonitorData.rfds)) {  // TCPソケット
        ret = recv_ok(&InetData, &MonitorData, &audioStream, &RecAndSendData);
        if (ret) {
          status = SPEAKING;
          fprintf(stderr, "speaking\n");
          continue;
        }
      }
    }

    else if (status == RINGING) {   // 相手から呼び出されている場合
      if (FD_ISSET(STDIN_FILENO, &MonitorData.rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字の削除

        if (strcmp(cbuf, "yes") == 0) {   // TODO: noの場合
          ret = recv_ok(&InetData, &MonitorData, &audioStream, &RecAndSendData);
          if (ret) {
            status = SPEAKING;
            fprintf(stderr, "speaking\n");
            continue;
          }
        }
      }
    }

    else if (status == SPEAKING) {  // 通話中の場合
      if (FD_ISSET(InetData.udp_sock, &MonitorData.rfds)) {  // UDPソケット
        ret = recv_and_play(&InetData);
      } else if (FD_ISSET(STDIN_FILENO, &MonitorData.rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字を削除
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "quit") == 0) {   // quitコマンド
          ret = stop_speaking(&InetData, audioStream);
          break;
        }
      }
    }
  }
  
  done(paNoError);

  return 0;
}
