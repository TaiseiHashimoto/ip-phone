#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include "phone.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static PaStream *audioStream;

void create_connection(char *ot_ip_addr, int ot_tcp_port) {
  int ret;

  // 相手のアドレスを設定
  InetData.ot_tcp_port = ot_tcp_port;
  InetData.ot_ip_addr = ot_ip_addr;
  ret = inet_aton(InetData.ot_ip_addr, &InetData.ot_tcp_addr.sin_addr);
  if (ret == 0) die("inet", paNoError);
  InetData.ot_tcp_addr.sin_port = htons(InetData.ot_tcp_port);

  ret = inet_aton(InetData.ot_ip_addr, &InetData.ot_udp_addr.sin_addr);
  if (ret == 0) die("inet", paNoError);
  InetData.ot_udp_addr.sin_port = htons(InetData.ot_udp_port);

  // TCPで接続する
  InetData.tcp_s = socket(PF_INET, SOCK_STREAM, 0);
  ret = connect(InetData.tcp_s, (struct sockaddr *)&InetData.ot_tcp_addr, sizeof(struct sockaddr_in));
  if (ret == -1) die("connect", paNoError);

  // Gtkのメインループで監視
  MonitorData.g_tcp_s = g_io_channel_unix_new(InetData.tcp_s);
  g_io_add_watch(MonitorData.g_tcp_s, G_IO_IN, assign_task, NULL);

  // INVITEメッセージを送信
  // この際自分のUDPポートを相手に通知する(TCPポートはaccept時にわかるので不要)
  // 形式 : INVITE <my UDP port>
  char cbuf[CHAR_BUF];
  sprintf(cbuf, "INVITE %d", InetData.my_udp_port);
  ret = send(InetData.tcp_s, cbuf, strlen(cbuf) + 1, 0);  // ヌル文字まで送信するのでstrlen+1
  if (ret == -1) die("send", paNoError);

  SessionStatus = INVITING;
  fprintf(stderr, "inviting\n");
}

gboolean accept_connection(GIOChannel *s, GIOCondition c, gpointer d) {
  int ret;
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  InetData.tcp_s = accept(InetData.tcp_sock, (struct sockaddr *)&InetData.ot_tcp_addr, &ot_addr_len);
  if (InetData.tcp_s == -1) {
    if (errno != EAGAIN) die("accept", paNoError);
    // EAGAINはデータが読み込めなかった場合
    // 送信側がconnect => 送信側がキャンセル => 受信側がaccept などの場合に発生する
    // 特にエラーではないのでdieしない
    return TRUE;
  } else {
    // 相手のIPアドレスを文字列として取得
    InetData.ot_ip_addr = inet_ntoa(InetData.ot_tcp_addr.sin_addr);
    if (InetData.ot_ip_addr == NULL) die("inet_ntoa", paNoError);
    // 相手のTCPポート番号を文字列として取得
    InetData.ot_tcp_port = ntohs(InetData.ot_tcp_addr.sin_port);
    // UDPのアドレスを設定
    ret = inet_aton(InetData.ot_ip_addr, &InetData.ot_udp_addr.sin_addr);
    if (ret == 0) die("inet", paNoError);

    // Gtkのメインループで監視
    MonitorData.g_tcp_s = g_io_channel_unix_new(InetData.tcp_s);
    g_io_add_watch(MonitorData.g_tcp_s, G_IO_IN, assign_task, NULL);
  }

  SessionStatus = NEGOTIATING;
  fprintf(stderr, "negotiating\n");

  return TRUE;
}

void recv_invitation() {
  int ret;
  char cbuf[CHAR_BUF];

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, 0);
  if (ret == -1) die("recv", paNoError);

  char *str = strtok(cbuf, " ");
  if (strcmp(str, "INVITE") == 0) {
    str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
    if (str == NULL) die("argument (UDP port)", paNoError);
    InetData.ot_udp_port = atoi(str);
    InetData.ot_udp_addr.sin_port = htons(InetData.ot_udp_port);

    SessionStatus = RINGING;
    fprintf(stderr, "answer ? ");

    ringing();
  }
}

void send_ok() {
  int ret;
  PaError err;
  char cbuf[CHAR_BUF];

  // OKメッセージを送信
  // この際自分のUDPポートを相手に通知する
  // 形式 : OK <my UDP port>
  sprintf(cbuf, "OK %d", InetData.my_udp_port);
  ret = send(InetData.tcp_s, cbuf, strlen(cbuf) + 1, 0);
  if (ret == -1) die("send", paNoError);

  
  open_rec(&audioStream);      // 録音、送信の開始
  err = Pa_StartStream(audioStream);
  if (err != paNoError) die(NULL, err);

  MonitorData.g_udp_sock = g_io_channel_unix_new(InetData.udp_sock);
  g_io_add_watch(MonitorData.g_udp_sock, G_IO_IN, recv_and_play, NULL);

  SessionStatus = SPEAKING;
  fprintf(stderr, "speaking\n");
}

void recv_ok() {
  int ret;
  PaError err;
  char cbuf[CHAR_BUF];

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, 0);
  if (ret == -1) die("recv", paNoError);

  char *str = strtok(cbuf, " ");
  if (strcmp(str, "OK") == 0) {    // TODO: NOの場合
    str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
    if (str == NULL) die("argument (UDP port)", paNoError);
    InetData.ot_udp_port = atoi(str);
    InetData.ot_udp_addr.sin_port = htons(InetData.ot_udp_port);
    
    open_rec(&audioStream);      // 録音、送信の開始
    err = Pa_StartStream(audioStream);
    if (err != paNoError) die(NULL, err);

    MonitorData.g_udp_sock = g_io_channel_unix_new(InetData.udp_sock);
    g_io_add_watch(MonitorData.g_udp_sock, G_IO_IN, recv_and_play, NULL);

    SessionStatus = SPEAKING;
    fprintf(stderr, "speaking\n");

    speaking();
  }
}

void send_bye() {
  PaError err;
  int ret;

  err = Pa_StopStream(audioStream);      // portaudioストリームを停止
  if (err != paNoError) die(NULL, err);
  err = Pa_CloseStream(audioStream);     // portaudioストリームをクローズ
  if (err != paNoError) die(NULL, err);

  char cbuf[CHAR_BUF];
  strcpy(cbuf, "BYE");
  ret = send(InetData.tcp_s, cbuf, strlen(cbuf) + 1, 0);
  if (ret == -1) die("send", paNoError);

  SessionStatus = NO_SESSION;
  fprintf(stderr, "stop speaking\n");
}

void recv_bye() {
  PaError err;
  int ret;
  char cbuf[CHAR_BUF];

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, 0);
  if (ret == -1) die("recv", paNoError);

  if (strcmp(cbuf, "BYE") == 0) {
    err = Pa_StopStream(audioStream);      // portaudioストリームを停止
    if (err != paNoError) die(NULL, err);
    err = Pa_CloseStream(audioStream);     // portaudioストリームをクローズ
    if (err != paNoError) die(NULL, err);

    SessionStatus = NO_SESSION;
    fprintf(stderr, "stop speaking\n");
  }

  stop_speaking();
}

gboolean assign_task(GIOChannel *s, GIOCondition c, gpointer d) {
  switch (SessionStatus) {
  case NO_SESSION:
    break;
  case NEGOTIATING:   // セッションの確立中の場合
    recv_invitation();
    break;
  case INVITING:      // 呼び出し中の場合
    recv_ok();
    break;
  case RINGING:
    break;
  case SPEAKING:    // 通話中の場合
    recv_bye();
    break;
  case QUIT:
    break;
  }

  return TRUE;
}
