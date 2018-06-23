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

int create_connection(InetData_t *InetData, MonitorData_t *MonitorData, char *str) {
  int ret;

  // 標準入力のコマンドから、相手のIPアドレスとポート番号を取得する
  InetData->ot_ip_addr = strtok(NULL, " ");
  if (InetData->ot_ip_addr == NULL) die("argument (IP addr)", paNoError);

  str = strtok(NULL, " ");
  if (str == NULL) die("argument (TCP port)", paNoError);
  InetData->ot_tcp_port = atoi(str);

  str = strtok(NULL, " ");
  if (str == NULL) die("argument (UDP port)", paNoError);
  InetData->ot_udp_port = atoi(str);

  // 相手のアドレスを設定
  ret = inet_aton(InetData->ot_ip_addr, &InetData->ot_tcp_addr.sin_addr);
  if (ret == 0) die("inet", paNoError);
  InetData->ot_tcp_addr.sin_port = htons(InetData->ot_tcp_port);

  ret = inet_aton(InetData->ot_ip_addr, &InetData->ot_udp_addr.sin_addr);
  if (ret == 0) die("inet", paNoError);
  InetData->ot_udp_addr.sin_port = htons(InetData->ot_udp_port);

  // TCPで接続する
  InetData->tcp_s = socket(PF_INET, SOCK_STREAM, 0);
  ret = connect(InetData->tcp_s, (struct sockaddr *)&InetData->ot_tcp_addr, sizeof(struct sockaddr_in));
  if (ret == -1) die("connect", paNoError);

  // INVITEメッセージを送信
  // この際自分のUDPポートを相手に通知する(TCPポートはaccept時にわかるので不要)
  // 形式 : INVITE <my UDP port>
  char cbuf[CHAR_BUF];
  sprintf(cbuf, "INVITE %d", InetData->my_udp_port);
  ret = send(InetData->tcp_s, cbuf, strlen(cbuf) + 1, 0);  // ヌル文字まで送信するのでstrlen+1
  if (ret == -1) die("send", paNoError);

  FD_SET(InetData->tcp_s, &MonitorData->rfds_org);     // TCPソケットを監視対象に追加
  MonitorData->max_fd = max(MonitorData->max_fd, InetData->tcp_s);

  return 1;
}

int accept_connection(InetData_t *InetData, MonitorData_t *MonitorData) {
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  InetData->tcp_s = accept(InetData->tcp_sock, (struct sockaddr *)&InetData->ot_tcp_addr, &ot_addr_len);
  if (InetData->tcp_s == -1) {
    if (errno != EAGAIN) die("accept", paNoError);
    // EAGAINはデータが読み込めなかった場合
    // 送信側がconnect => 送信側がキャンセル => 受信側がaccept などの場合に発生する
    // 特にエラーではないのでdieしない
    return 0;
  } else {
    // 相手のIPアドレスを文字列として取得
    InetData->ot_ip_addr = inet_ntoa(InetData->ot_tcp_addr.sin_addr);
    if (InetData->ot_ip_addr == NULL) die("inet_ntoa", paNoError);

    FD_SET(InetData->tcp_s, &MonitorData->rfds_org);    // acceptしたTCPソケットを監視対象に追加
    MonitorData->max_fd = max(MonitorData->max_fd, InetData->tcp_s);
  }

  return 1;
}

int recv_invitation(InetData_t *InetData, MonitorData_t *MonitorData) {
  int ret;
  char cbuf[CHAR_BUF];

  ret = recv(InetData->tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
  if (ret == -1 && errno != EAGAIN) die("recv", paNoError);
  char *str = strtok(cbuf, " ");
  if (strcmp(str, "INVITE") == 0) {
    str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
    if (str == NULL) die("argument (UDP port)", paNoError);
    InetData->ot_udp_port = atoi(str);
    InetData->ot_udp_addr.sin_port = htons(InetData->ot_udp_port);
  }

  return 1;
}

int recv_ok(InetData_t *InetData, MonitorData_t *MonitorData, PaStream **audioStream, RecAndSendData_t *RecAndSendData) {
  int ret;
  PaError err;
  char cbuf[CHAR_BUF];

  ret = recv(InetData->tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
  fprintf(stderr, "recved \"%s\"\n", cbuf);
  if (ret == -1 && errno != EAGAIN) die("recv", paNoError);
  if (strcmp(cbuf, "OK") == 0) {    // TODO: NOの場合
    // 録音、送信の開始
    fprintf(stderr, "recv OK\n");
    open_rec(audioStream, RecAndSendData);
    err = Pa_StartStream(*audioStream);
    if (err != paNoError) die(NULL, err);

    return 1;
  }

  return 0;
}

int send_ok(InetData_t *InetData, MonitorData_t *MonitorData, PaStream **audioStream, RecAndSendData_t *RecAndSendData) {
  int ret;
  PaError err;
  char cbuf[CHAR_BUF];

  // OKメッセージを送信
  strcpy(cbuf, "OK");
  ret = send(InetData->tcp_s, cbuf, strlen(cbuf) + 1, 0);
  if (ret == -1) die("send", paNoError);
  // fprintf(stderr, "%s : %d\n", InetData->ot_ip_addr, InetData->ot_udp_port);

  // 録音、送信の開始
  open_rec(audioStream, RecAndSendData);
  err = Pa_StartStream(*audioStream);
  if (err != paNoError) die(NULL, err);

  return 1;
}

int recv_and_play(InetData_t *InetData) {
  short sbuf[SOUND_BUF];
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  int n = recvfrom(InetData->udp_sock, sbuf, SOUND_BUF, MSG_DONTWAIT, (struct sockaddr *)&InetData->ot_udp_addr, &ot_addr_len);
  if (n == -1 && errno != EAGAIN) die("recv", paNoError);
  write(1, sbuf, n);    // 標準出力に書き込み TODO: playもportaudioに変更
  // fprintf(stderr, "recvfrom %d byte\n", n);

  return 1;
}

int stop_speaking(InetData_t *InetData, PaStream *audioStream) {
  PaError err;

  err = Pa_StopStream(audioStream);      // portaudioストリームを停止
  if (err != paNoError) die(NULL, err);
  err = Pa_CloseStream(audioStream);     // portaudioストリームをクローズ
  if (err != paNoError) die(NULL, err);

  return 1;
}
