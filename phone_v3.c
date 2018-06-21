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
#include "portaudio.h"

#define SAMPLE_RATE 8000    // サンプル周波数(Hz)
#define SOUND_BUF 8000      // 音声データの受信用バッファ長
#define CHAR_BUF 200        // セッション管理用文字列のバッファ長
#define QUE_LEN 10          // 保留中の接続のキューの最大長
#define SILENT 100          // 無音とみなす音量の閾値  TODO: 自動で調整
#define SILENT_FRAMES 8000  // 沈黙とみなす、無音の最小連続フレーム数

enum Status {
  NO_SESSION,     // セッションを開始していない
  NEGOTIATING,    // セッションの確立中
  INVITING,       // 呼び出し中
  RINGING,        // コール受信中
  SPEAKING        // 通話中
};

#define NO_SESSION  0
#define NEGOTIATING 1
#define INVITING    2
#define RINGING     3
#define SPEAKING    4

// UDPでの送信用の情報を持つ構造体(rec_and_send()で使用)
typedef struct {
  int sock;                   // ソケットID
  struct sockaddr_in *addr;   // 送信相手アドレス構造体へのポインタ
  int silent_frames;          // 無音が連続しているフレーム数
} UDPData_t;

/**
 *  終了時に後始末をする
 *    err : portaudioのエラーハンドリング用変数
 */
int done(PaError err) {
  const PaHostErrorInfo* herr;

  Pa_Terminate();

  if(err != paNoError) {
    fprintf(stderr, "An error occured while using portaudio\n");
    if(err == paUnanticipatedHostError) {
      fprintf(stderr, " unanticipated host error.\n");
      herr = Pa_GetLastHostErrorInfo();
      if (herr) {
        fprintf( stderr, " Error number: %ld\n", herr->errorCode );
        if (herr->errorText) {
          fprintf(stderr, " Error text: %s\n", herr->errorText );
        }
      } else {
        fprintf(stderr, " Pa_GetLastHostErrorInfo() failed!\n" );
      }
    } else {
      fprintf(stderr, " Error number: %d\n", err );
      fprintf(stderr, " Error text: %s\n", Pa_GetErrorText( err ) );
    }
    err = 1;
  }

  fprintf(stderr, "bye\n");
  exit(err);
}

/**
 *  異常時に中断
 *    message : エラーメッセージ(perror()で使用)
 *    err : portaudioのエラーハンドリング用変数
 */
void die(char *message, PaError err) {   // TODO: エラーハンドリング(die()で終了させない)
  if (message) {
    perror(message);
  }
  done(err);
}

int max(int a, int b) {
  return a > b ? a : b;
}

int min (int a, int b) {
  return a < b ? a : b;
}

/**
 *  正の余りを求める(a%bはa<0のとき負になる)
 *  現在未使用(リングバッファで使用予定)
 *    a : 被除数
 *    b : 除数
 */
int positive_mod(int a, int b) {
  return a >= 0 ? a % b : a % b + b;
}

/**
 *  録音し、UDPを用いて送信する　portaudioのコールバック関数
 *    inputBuffer : portaudioの入力バッファ　録音データをここから読み出せる
 *    outputBuffer : portaudioの出力バッファ(使用しない)
 *    framesPerBuffer : 1回のコールで扱うバッファのサイズ
 *    timeInfo : portaudioの時間に関する構造体へのポインタ(使用しない)
 *    statusFlag : portaudioの状態に関する構造体へのポインタ(使用しない)
 *    userData : UDPData_t型の構造体へのポインタ
 */
static int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
  int ret;
  short *in = (short *)inputBuffer; // 16bit(2Byte)で符号化しているのでshort型にキャスト
  UDPData_t *data = userData;       // UDPでの送信用の情報

  int silent = 1;   // 沈黙かどうかのフラグ(沈黙なら送らない)

  for (int i = 0; i < framesPerBuffer; i++) {
    if (abs(in[i]) > SILENT) {    // 音量が無音の閾値より大きい場合
      silent = 0;                 // フラグを下ろす
      data->silent_frames = 0;    // 無音が連続しているフレーム数を0にリセット
      break;
    }
  }

  if (silent) {
    /* 無音が連続しているフレーム数に今回のフレーム数を足すが、SILENT_FRAMESより
       大きい値にはしない(しても意味がないし、オーバーフローが起こりうる) */
    data->silent_frames = min(data->silent_frames + framesPerBuffer, SILENT_FRAMES);
  }

  // 今回無音でないか、無音が連続しているフレーム数がSILENT_FREMESより少ない場合
  if (!silent || data->silent_frames < SILENT_FRAMES) {
    ret = sendto(data->sock, in, 2 * framesPerBuffer, 0, (struct sockaddr *)data->addr, sizeof(struct sockaddr_in));
    if (ret == -1) die("sendto", paNoError);
  }

  in += framesPerBuffer;  // move pointer forward

  return paContinue;
}

/**
 *  portaudioの設定
 *    stream : portaudioのストリーム
 *    inputParameters : portaudioの入力設定用パラメータ
 *    udp_data : UDPでの送信用の情報
 */
void open_rec(PaStream **stream, PaStreamParameters *inputParameters, UDPData_t *udp_data) {
  PaError err;

  err = Pa_Initialize();
  
  inputParameters->device = Pa_GetDefaultInputDevice();
  if (inputParameters->device == paNoDevice) {
    fprintf(stderr,"Error: No input default device.\n");
    die(NULL, err);
  }
  inputParameters->channelCount = 1;          // モノラル
  inputParameters->sampleFormat = paInt16;    // 16bit 整数
  inputParameters->suggestedLatency = Pa_GetDeviceInfo(inputParameters->device)->defaultLowInputLatency;  // レイテンシの設定
  inputParameters->hostApiSpecificStreamInfo = NULL;  // デバイスドライバの設定

  err = Pa_OpenStream(stream,
                      inputParameters,
                      NULL,               // outputは使用しない
                      SAMPLE_RATE,        // サンプリング周波数
                      paFramesPerBufferUnspecified,  // 自動的に最適なバッファサイズが設定される
                      paClipOff,          // クリップしたデータは使わない
                      rec_and_send,       // コールバック関数
                      udp_data);          // UDPでの送信用の情報
  if (err != paNoError) die(NULL, err);
}

int main(int argc, char **argv) {
  PaError err = paNoError;
  int ret;

  if (argc != 3) die("argument", err);  // 引数の個数チェック

  int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);

  // 自分のポート番号は引数から設定する
  int my_tcp_port = atoi(argv[1]);  
  int my_udp_port = atoi(argv[2]);
  // 相手のポート番号は標準入力からのコマンド、または相手からの通知で設定する
  int ot_tcp_port, ot_udp_port;

  // アドレス構造体の設定
  // 相手のIPアドレスは標準入力からのコマンド、または相手からの通知で設定する
  struct sockaddr_in my_tcp_addr, my_udp_addr, ot_tcp_addr, ot_udp_addr;
  my_tcp_addr.sin_family = AF_INET;
  my_tcp_addr.sin_addr.s_addr = INADDR_ANY;
  my_tcp_addr.sin_port = htons(my_tcp_port);
  my_udp_addr.sin_family = AF_INET;
  my_udp_addr.sin_addr.s_addr = INADDR_ANY;
  my_udp_addr.sin_port = htons(my_udp_port);
  ot_tcp_addr.sin_family = AF_INET;
  ot_udp_addr.sin_family = AF_INET;
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);
  char *ot_ip_addr;   // 相手のIPアドレス(文字列)

  int reuse = 1;   // "Address already in use"のエラーを回避する
  setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
  int nonblock = 1;   // select()を使用するため、ソケットをノンブロッキングに設定
  ioctl(tcp_sock, FIONBIO, &nonblock);

  ret = bind(tcp_sock, (struct sockaddr *)&my_tcp_addr, sizeof(my_tcp_addr));
  if (ret == -1) die("bind (tcp)", err);
  ret = bind(udp_sock, (struct sockaddr *)&my_udp_addr, sizeof(my_udp_addr));
  if (ret == -1) die("bind (udp)", err);

  listen(tcp_sock, QUE_LEN);

  // select()のための設定
  // TCP/UDPソケット、標準入力を監視する
  fd_set rfds, rfds_org;
  FD_ZERO(&rfds_org);
  FD_SET(tcp_sock, &rfds_org);
  FD_SET(udp_sock, &rfds_org);
  FD_SET(0, &rfds_org);

  enum Status status = NO_SESSION;       // セッションのステータスを初期化
  int max_fd = max(tcp_sock, udp_sock);  // 監視するファイルディスクリプタの最大値(select()で使用)
  int tcp_s;                // acceptしたTCPソケットID
  short sbuf[SOUND_BUF/2];  // 音声データのバッファ
  char cbuf[CHAR_BUF];      // セッション管理用の文字列のバッファ

  PaStream *audioStream;              // portaudioのストリーム
  PaStreamParameters inputParameters; // portaudioの入力設定用の構造体
  UDPData_t udp_data;             // UDPでの送信用の情報
  udp_data.sock = udp_sock;       // ソケットID
  udp_data.addr = &ot_udp_addr;   // アドレス構造体のポインタ
  udp_data.silent_frames = 0;     // 無音が連続しているフレーム数(0に初期化)

  while (1) {   // TODO: セッション未開始時もUDPで受信してしまう => 無限ループ
    memcpy(&rfds, &rfds_org, sizeof(fd_set));
    select(max_fd + 1, &rfds, NULL, NULL, NULL);

    if (status == NO_SESSION) {
      if (FD_ISSET(tcp_sock, &rfds)) {  // TCP socket (not accepted)
        tcp_s = accept(tcp_sock, (struct sockaddr *)&ot_tcp_addr, &ot_addr_len);
        if (tcp_s == -1) {
          if (errno != EAGAIN) die("accept", err);
          // EAGAINはデータが読み込めなかった場合
          // 送信側がconnect => 送信側がキャンセル => 受信側がaccept などの場合に発生する
          // 特にエラーではないのでdieしない
        } else {
          status = NEGOTIATING;   // ステータスを"セッションの確立中"に変更
          fprintf(stderr, "negotiating\n");

          // 相手のIPアドレスを文字列として取得
          ot_ip_addr = inet_ntoa(ot_tcp_addr.sin_addr);
          if (ot_ip_addr == NULL) die("inet_ntoa", err);
          // 相手のIPアドレスを設定
          ot_tcp_addr.sin_addr.s_addr = ot_tcp_addr.sin_addr.s_addr;

          FD_SET(tcp_s, &rfds_org);    // acceptしたTCPソケットを監視対象に追加
          max_fd = max(max_fd, tcp_s);
          continue;
        }
      }
      if (FD_ISSET(0, &rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字を削除
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "call") == 0) {   // callコマンド
          // 標準入力のコマンドから、相手のIPアドレスとポート番号を取得する
          ot_ip_addr = strtok(NULL, " ");
          if (ot_ip_addr == NULL) die("argument (IP addr)", err);
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (TCP port)", err);
          ot_tcp_port = atoi(str);
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (UDP port)", err);
          ot_udp_port = atoi(str);

          // 相手のアドレスを設定
          ret = inet_aton(ot_ip_addr, &ot_tcp_addr.sin_addr);
          if (ret == 0) die("inet", err);
          ot_tcp_addr.sin_port = htons(ot_tcp_port);
          ret = inet_aton(ot_ip_addr, &ot_udp_addr.sin_addr);
          if (ret == 0) die("inet", err);
          ot_udp_addr.sin_port = htons(ot_udp_port);

          // TCPで接続する
          tcp_s = socket(PF_INET, SOCK_STREAM, 0);
          ret = connect(tcp_s, (struct sockaddr *)&ot_tcp_addr, sizeof(ot_tcp_addr));
          if (ret == -1) die("connect", err);

          // INVITEメッセージを送信
          // この際自分のUDPポートを相手に通知する(TCPポートはaccept時にわかるので不要)
          // 形式 : INVITE <my UDP port>
          sprintf(cbuf, "INVITE %d", my_udp_port);
          ret = send(tcp_s, cbuf, strlen(cbuf) + 1, 0);
          if (ret == -1) die("send", err);

          status = INVITING;    // セッションのステータスを"コール受信中"に変更
          fprintf(stderr, "inviting\n");

          FD_SET(tcp_s, &rfds_org);     // TCPソケットを監視対象に追加
          max_fd = max(max_fd, tcp_s);
          continue;
        }
      }
    }

    else if (status == NEGOTIATING) {  // セッションの確立中の場合
      if (FD_ISSET(tcp_s, &rfds)) {   // TCPソケット
        ret = recv(tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        char *str = strtok(cbuf, " ");
        if (strcmp(str, "INVITE") == 0) {
          str = strtok(NULL, " ");    // 相手のUDPポートをINVITEメッセージから取得する
          if (str == NULL) die("argument (UDP port)", err);
          ot_udp_port = atoi(str);
          ot_udp_addr.sin_port = htons(ot_udp_port);

          status = RINGING;   // セッションのステータスを"コール受信中"に変更
          fprintf(stderr, "answer ? ");
        }
      }
    }

    else if (status == INVITING) {   // 呼び出し中の場合
      if (FD_ISSET(tcp_s, &rfds)) {  // TCPソケット
        ret = recv(tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        if (strcmp(cbuf, "OK") == 0) {    // TODO: NOの場合
          // 録音、送信の開始
          open_rec(&audioStream, &inputParameters, &udp_data);
          err = Pa_StartStream(audioStream);
          if (err != paNoError) die(NULL, err);

          status = SPEAKING;    // セッションのステータスを"通話中"に変更
          fprintf(stderr, "speaking\n");
          continue;
        }
      }
    }

    else if (status == RINGING) {   // 相手から呼び出されている場合
      if (FD_ISSET(0, &rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字の削除
        if (strcmp(cbuf, "yes") == 0) {   // TODO: noの場合
          // OKメッセージを送信
          strcpy(cbuf, "OK");
          ret = send(tcp_s, cbuf, strlen(cbuf) + 1, 0);
          if (ret == -1) die("send", err);
          // fprintf(stderr, "%s : %d\n", ot_ip_addr, ot_udp_port);

          // 録音、送信の開始
          open_rec(&audioStream, &inputParameters, &udp_data);
          err = Pa_StartStream(audioStream);
          if (err != paNoError) die(NULL, err);

          status = SPEAKING;    // セッションのステータスを"通話中"に変更
          fprintf(stderr, "speaking\n");
          continue;
        }
      }
    }

    else if (status == SPEAKING) {  // 通話中の場合
      if (FD_ISSET(udp_sock, &rfds)) {  // UDPソケット
        ret = recvfrom(udp_sock, sbuf, SOUND_BUF, MSG_DONTWAIT, (struct sockaddr *)&ot_udp_addr, &ot_addr_len);
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        ret = write(1, sbuf, ret);    // 標準出力に書き込み TODO: playもportaudioに変更
        // fprintf(stderr, "recvfrom %d byte\n", ret);
      } else if (FD_ISSET(0, &rfds)) {  // 標準入力
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // 改行文字を削除
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "quit") == 0) {   // quitコマンド
          err = Pa_StopStream(audioStream);      // portaudioストリームを停止
          if (err != paNoError) die(NULL, err);
          err = Pa_CloseStream(audioStream);     // portaudioストリームをクローズ
          if (err != paNoError) die(NULL, err);
          status = NO_SESSION;    // セッションのステータスを初期化
          break;
        }
      }
    }
  }
  
  done(err);

  return 0;
}
