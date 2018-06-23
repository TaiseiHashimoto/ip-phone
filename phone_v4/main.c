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
#include <sys/time.h>
#include <errno.h>
#include "phone.h"

enum Status SessionStatus;
InetData_t InetData;
MonitorData_t MonitorData;

int main(int argc, char **argv) {
  int ret;

  if (argc != 2) die("argument", paNoError);  // 引数の個数チェック

  // アドレス構造体の設定
  InetData.tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  InetData.udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
  InetData.my_tcp_port = atoi(argv[1]);
  InetData.my_tcp_addr.sin_family = AF_INET;
  InetData.my_tcp_addr.sin_addr.s_addr = INADDR_ANY;
  InetData.my_tcp_addr.sin_port = htons(InetData.my_tcp_port);
  InetData.my_udp_addr.sin_family = AF_INET;
  InetData.my_udp_addr.sin_addr.s_addr = INADDR_ANY;
  InetData.my_udp_addr.sin_port = htons(0);   // 自動割り当て
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

  // 実際に割り当てられたアドレスを代入
  socklen_t addrlen = sizeof(struct sockaddr_in);
  ret = getsockname(InetData.udp_sock, (struct sockaddr *)&InetData.my_udp_addr, &addrlen);
  if (ret == -1) die("getsockname", paNoError);
  InetData.my_udp_port = ntohs(InetData.my_udp_addr.sin_port);

  listen(InetData.tcp_sock, QUE_LEN);

  // select()のための設定
  // TCP/UDPソケット、標準入力を監視する
  struct timeval timeout;

  FD_ZERO(&MonitorData.rfds_org);
  FD_SET(InetData.tcp_sock, &MonitorData.rfds_org);
  FD_SET(InetData.udp_sock, &MonitorData.rfds_org);
  // FD_SET(STDIN_FILENO, &MonitorData.rfds_org);

  SessionStatus = NO_SESSION;   // セッションのステータスを初期化

  // 監視するファイルディスクリプタの最大値(select()で使用)
  MonitorData.max_fd = max(InetData.tcp_sock, InetData.udp_sock);

  prepare_to_display(&argc, &argv);   // Gtkの初期設定

  while (1) {   // TODO: セッション未開始時もUDPで受信してしまう => 無限ループ
    timeout.tv_sec = timeout.tv_usec = 0;   // ブロックしない
    MonitorData.rfds = MonitorData.rfds_org;
    select(MonitorData.max_fd + 1, &MonitorData.rfds, NULL, NULL, &timeout);    

    // gtkのイベント処理
    while (gtk_events_pending())
      gtk_main_iteration();

    switch (SessionStatus) {
    case NO_SESSION:
      if (FD_ISSET(InetData.tcp_sock, &MonitorData.rfds))  // TCP socket (not accepted)
        accept_connection();
      break;

    case NEGOTIATING:   // セッションの確立中の場合
      if (FD_ISSET(InetData.tcp_s, &MonitorData.rfds))   // TCPソケット
        recv_invitation();
      break;

    case INVITING:      // 呼び出し中の場合
      if (FD_ISSET(InetData.tcp_s, &MonitorData.rfds))  // TCPソケット
        recv_ok();
      break;

    case RINGING:
      break;

    case SPEAKING:    // 通話中の場合
      if (FD_ISSET(InetData.udp_sock, &MonitorData.rfds))  // UDPソケット
        recv_and_play();
      if (FD_ISSET(InetData.tcp_s, &MonitorData.rfds))
        recv_bye();
      break;

    case QUIT:
      break;
    }

    if (SessionStatus == QUIT) break;
  }
  
  done(paNoError);

  return 0;
}
