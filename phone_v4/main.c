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
  InetData.my_udp_addr.sin_port = 0;   // 自動割り当て
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

  // 実際に割り当てられたアドレスを取得
  socklen_t addrlen = sizeof(struct sockaddr_in);
  ret = getsockname(InetData.udp_sock, (struct sockaddr *)&InetData.my_udp_addr, &addrlen);
  if (ret == -1) die("getsockname", paNoError);
  InetData.my_udp_port = ntohs(InetData.my_udp_addr.sin_port);

  listen(InetData.tcp_sock, QUE_LEN);

  SessionStatus = NO_SESSION;   // セッションのステータスを初期化

  PaError err = Pa_Initialize();
  if (err != paNoError) die(NULL, err);   // PortAudioの初期化

  prepare_to_display(&argc, &argv);   // Gtkの初期設定

  MonitorData.g_tcp_sock = g_io_channel_unix_new(InetData.tcp_sock);

  MonitorData.tcp_sock_tag = g_io_add_watch(MonitorData.g_tcp_sock, G_IO_IN, accept_connection, NULL);

  gtk_main();  // Gtkのメインループ

  if (MonitorData.g_tcp_sock)
    g_io_channel_unref(MonitorData.g_tcp_sock);
  if (MonitorData.g_tcp_s)
    g_io_channel_unref(MonitorData.g_tcp_s);
  if (MonitorData.g_udp_sock)
    g_io_channel_unref(MonitorData.g_udp_sock);
  
  done(paNoError);
  close(InetData.tcp_sock);
  close(InetData.tcp_s);
  close(InetData.udp_sock);

  return 0;
}
