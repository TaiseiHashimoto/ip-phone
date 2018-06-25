#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <portaudio.h>
#include "phone.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

RecAndSendData_t RecAndSendData;   // rec_and_send()で使用
RecvAndPlayData_t RecvAndPlayData; // recv_and_play()で使用

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
 *  portaudioの設定
 *    audioStream : portaudioの入力用ストリーム
 */
void open_rec(PaStream **audioStream) {
  PaError err;
  PaStreamParameters inputParameters;
  
  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    fprintf(stderr,"Error: No input default device.\n");
    die(NULL, paNoError);
  }
  inputParameters.channelCount = 1;          // モノラル
  inputParameters.sampleFormat = paInt16;    // 16bit 整数
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;  // レイテンシの設定
  inputParameters.hostApiSpecificStreamInfo = NULL;  // デバイスドライバの設定

  err = Pa_OpenStream(audioStream,
                      &inputParameters,
                      NULL,               // outputは使用しない
                      SAMPLE_RATE,        // サンプリング周波数
                      paFramesPerBufferUnspecified,  // 自動的に最適なバッファサイズが設定される
                      paClipOff,          // クリップしたデータは使わない
                      rec_and_send,       // コールバック関数
                      NULL);
  if (err != paNoError) die(NULL, err);
}


/**
 *  録音し、UDPを用いて送信する　portaudioのコールバック関数
 *    inputBuffer : portaudioの入力バッファ　録音データをここから読み出せる
 *    outputBuffer : portaudioの出力バッファ(使用しない)
 *    framesPerBuffer : 1回のコールで扱うバッファのサイズ
 *    timeInfo : portaudioの時間に関する構造体へのポインタ(使用しない)
 *    statusFlag : portaudioの状態に関する構造体へのポインタ(使用しない)
 *    userData : コールバック関数に渡すデータ(使用しない)
 */
int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
  int ret;
  short *in = (short *)inputBuffer; // 16bit(2Byte)で符号化しているのでshort型にキャスト

  int silent = 1;   // 沈黙かどうかのフラグ(沈黙なら送らない)

  for (int i = 0; i < framesPerBuffer; i++) {
    if (abs(in[i]) > SILENT) {    // 音量が無音の閾値より大きい場合
      silent = 0;                 // フラグを下ろす
      RecAndSendData.silent_frames = 0;    // 無音が連続しているフレーム数を0にリセット
      break;
    }
  }

  if (silent) {
    /* 無音が連続しているフレーム数に今回のフレーム数を足すが、SILENT_FRAMESより
       大きい値にはしない(しても意味がないし、オーバーフローが起こりうる) */
    RecAndSendData.silent_frames = min(RecAndSendData.silent_frames + framesPerBuffer, SILENT_FRAMES);
  }

  // 今回無音でないか、無音が連続しているフレーム数がSILENT_FREMESより少ない場合
  if (!silent || RecAndSendData.silent_frames < SILENT_FRAMES) {
    ret = sendto(InetData.udp_sock, in, 2 * framesPerBuffer, 0, (struct sockaddr *)&InetData.ot_udp_addr, sizeof(struct sockaddr_in));
    if (ret == -1) {
      fprintf(stderr, "udp : %s %d\n", inet_ntoa(InetData.ot_udp_addr.sin_addr), ntohs(InetData.ot_udp_addr.sin_port));
      die("sendto", paNoError);
    }
  }

  in += framesPerBuffer;  // move pointer forward

  return paContinue;
}


gboolean recv_and_play(GIOChannel *s, GIOCondition c, gpointer d) {
  short sbuf[SOUND_BUF/2];
  socklen_t ot_addr_len = sizeof(struct sockaddr_in);

  int n = recvfrom(InetData.udp_sock, sbuf, SOUND_BUF, 0, (struct sockaddr *)&InetData.ot_udp_addr, &ot_addr_len);
  if (n == -1) die("recv", paNoError);
  write(1, sbuf, n);

  return TRUE;
}
