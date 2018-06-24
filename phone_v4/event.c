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

static PaStream *inStream;
static PaStream *outStream;

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

  // INVITEメッセージを送信
  // この際自分のUDPポートを相手に通知する(TCPポートはaccept時にわかるので不要)
  // 形式 : INVITE <my UDP port>
  char cbuf[CHAR_BUF];
  sprintf(cbuf, "INVITE %d", InetData.my_udp_port);
  ret = send(InetData.tcp_s, cbuf, strlen(cbuf) + 1, 0);  // ヌル文字まで送信するのでstrlen+1
  if (ret == -1) die("send", paNoError);

  FD_SET(InetData.tcp_s, &MonitorData.rfds_org);     // TCPソケットを監視対象に追加
  MonitorData.max_fd = max(MonitorData.max_fd, InetData.tcp_s);

  SessionStatus = INVITING;
  fprintf(stderr, "inviting\n");
}

void accept_connection() {
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  InetData.tcp_s = accept(InetData.tcp_sock, (struct sockaddr *)&InetData.ot_tcp_addr, &ot_addr_len);
  if (InetData.tcp_s == -1) {
    if (errno != EAGAIN) die("accept", paNoError);
    // EAGAINはデータが読み込めなかった場合
    // 送信側がconnect => 送信側がキャンセル => 受信側がaccept などの場合に発生する
    // 特にエラーではないのでdieしない
    return;
  } else {
    // 相手のIPアドレスを文字列として取得
    InetData.ot_ip_addr = inet_ntoa(InetData.ot_tcp_addr.sin_addr);
    if (InetData.ot_ip_addr == NULL) die("inet_ntoa", paNoError);
    InetData.ot_tcp_port = ntohs(InetData.ot_tcp_addr.sin_port);

    FD_SET(InetData.tcp_s, &MonitorData.rfds_org);    // acceptしたTCPソケットを監視対象に追加
    MonitorData.max_fd = max(MonitorData.max_fd, InetData.tcp_s);
  }

  SessionStatus = NEGOTIATING;
  fprintf(stderr, "negotiating\n");
}

void recv_invitation() {
  int ret;
  char cbuf[CHAR_BUF];

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
  if (ret == -1 && errno != EAGAIN) die("recv", paNoError);

  char *str = strtok(cbuf, " ");
  if (strcmp(str, "INVITE") == 0) {
    str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
    if (str == NULL) die("argument (UDP port)", paNoError);
    InetData.ot_udp_port = atoi(str);
    InetData.ot_udp_addr.sin_port = htons(InetData.ot_udp_port);

    enable_answer(TRUE);    // 受信ボタンを有効化

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

  
  open_rec(&inStream);      // 録音、送信の開始
  err = Pa_StartStream(inStream);
  if (err != paNoError) die(NULL, err);
  open_play(&outStream);      // 受信、再生の開始
  err = Pa_StartStream(outStream);
  if (err != paNoError) die(NULL, err);

  enable_hang_up(TRUE);    // 終了ボタンを有効化

  SessionStatus = SPEAKING;
  fprintf(stderr, "speaking\n");
}

void recv_ok() {
  int ret;
  PaError err;
  char cbuf[CHAR_BUF];

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
  if (ret == -1 && errno != EAGAIN) die("recv", paNoError);

  char *str = strtok(cbuf, " ");
  if (strcmp(str, "OK") == 0) {    // TODO: NOの場合
    str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
    if (str == NULL) die("argument (UDP port)", paNoError);
    InetData.ot_udp_port = atoi(str);
    InetData.ot_udp_addr.sin_port = htons(InetData.ot_udp_port);
    
    open_rec(&inStream);      // 録音、送信の開始
    err = Pa_StartStream(inStream);
    if (err != paNoError) die(NULL, err);
    open_play(&outStream);      // 受信、再生の開始
    err = Pa_StartStream(outStream);
    if (err != paNoError) die(NULL, err);

    enable_hang_up(TRUE);    // 終了ボタンを有効化

    SessionStatus = SPEAKING;
    fprintf(stderr, "speaking\n");

    speaking();
  }
}

void send_bye() {
  PaError err;
  int ret;

  err = Pa_StopStream(inStream);      // portaudioストリームを停止
  if (err != paNoError) die(NULL, err);
  err = Pa_CloseStream(inStream);     // portaudioストリームをクローズ
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

  ret = recv(InetData.tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
  if (ret == -1 && errno != EAGAIN) die("recv", paNoError);

  if (strcmp(cbuf, "BYE") == 0) {
    err = Pa_StopStream(inStream);      // portaudioストリームを停止
    if (err != paNoError) die(NULL, err);
    err = Pa_CloseStream(inStream);     // portaudioストリームをクローズ
    if (err != paNoError) die(NULL, err);

    SessionStatus = NO_SESSION;
    fprintf(stderr, "stop speaking\n");
  }

  stop_speaking();
}
