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

#define SAMPLE_RATE 8000
#define SOUND_BUF 10000
#define CHAR_BUF 200
#define QUE_LEN 10

#define NO_SESSION  0
#define NEGOTIATING 1
#define INVITING    2
#define RINGING     3
#define SPEAKING    4

typedef struct {
  int sock;
  struct sockaddr_in *addr;
} UDPData_t;


int done(PaError err) {
  const PaHostErrorInfo*  herr;

  Pa_Terminate();

  if(err != paNoError) {
    fprintf( stderr, "An error occured while using portaudio\n" );
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

  if (err != paNoError) exit(err);
  return err;
}

void die(char *message, PaError err) {   // TODO: error handling (do not die)
  if (message) {
    perror(message);
  }
  done(err);
}

int max(int a, int b) {
  return a > b ? a : b;
}

static int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
  int ret;
  short *in = (short *)inputBuffer;
  socklen_t sock_len;
  UDPData_t *data = userData;

  ret = sendto(data->sock, in, 2 * framesPerBuffer, 0, (struct sockaddr *)data->addr, sizeof(struct sockaddr_in));
  if (ret == -1) die("sendto", paNoError);
  in += framesPerBuffer;  // move pointer forward

  return paContinue;
}

void open_rec(PaStream **stream, PaStreamParameters *inputParameters, UDPData_t *udp_data) {
  PaError err;

  err = Pa_Initialize();
  
  inputParameters->device = Pa_GetDefaultInputDevice();
  if (inputParameters->device == paNoDevice) {
    fprintf(stderr,"Error: No input default device.\n");
    die(NULL, err);
  }
  inputParameters->channelCount = 1;          // monoral
  inputParameters->sampleFormat = paInt16;    // 16 bit integer
  inputParameters->suggestedLatency = Pa_GetDeviceInfo(inputParameters->device)->defaultLowInputLatency;
  inputParameters->hostApiSpecificStreamInfo = NULL;

  // open stream
  err = Pa_OpenStream(stream,
                      inputParameters,
                      NULL,               // input only
                      SAMPLE_RATE,
                      paFramesPerBufferUnspecified,  // optimal number of frames selected automatically 
                      paClipOff,           // do not output out of range samples
                      rec_and_send,
                      udp_data);
  if (err != paNoError) die(NULL, err);
}

int main(int argc, char **argv) {
  PaError err = paNoError;
  int ret;

  if (argc != 3) die("argument", err);

  int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
  int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
  int my_tcp_port = atoi(argv[1]);
  int my_udp_port = atoi(argv[2]);
  int ot_tcp_port, ot_udp_port;
  char *ot_ip_addr;

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

  int reuse = 1;   // avoid error "Address already in use"
  setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));
  int nonblock = 1;   // set sockets as non-blocking (use select())
  ioctl(tcp_sock, FIONBIO, &nonblock);

  ret = bind(tcp_sock, (struct sockaddr *)&my_tcp_addr, sizeof(my_tcp_addr));
  if (ret == -1) die("bind (tcp)", err);
  ret = bind(udp_sock, (struct sockaddr *)&my_udp_addr, sizeof(my_udp_addr));
  if (ret == -1) die("bind (udp)", err);

  listen(tcp_sock, QUE_LEN);

  fd_set rfds, rfds_org; // monitor TCP/UDP sockets, stdin
  FD_ZERO(&rfds_org);
  FD_SET(tcp_sock, &rfds_org);
  FD_SET(udp_sock, &rfds_org);
  FD_SET(0, &rfds_org);

  int state = NO_SESSION;   // flag to tell if talk has already started
  int max_fd = max(tcp_sock, udp_sock);     // max file descriptor (used by select())
  int tcp_s;
  short sbuf[SOUND_BUF/2];
  char cbuf[CHAR_BUF];

  PaStream *audioStream;
  UDPData_t udp_data;
  udp_data.sock = udp_sock;
  udp_data.addr = &ot_udp_addr;
  PaStreamParameters inputParameters;

  while (1) {   // TODO: receive into UDP when NO_SESSION
    memcpy(&rfds, &rfds_org, sizeof(fd_set));
    select(max_fd + 1, &rfds, NULL, NULL, NULL);

    if (state == NO_SESSION) {
      if (FD_ISSET(tcp_sock, &rfds)) {  // TCP socket (not accepted)
        tcp_s = accept(tcp_sock, (struct sockaddr *)&ot_tcp_addr, &ot_addr_len);
        if (tcp_s == -1) {
          if (errno != EAGAIN) die("accept", err);
          // case EAGAIN : C connect => C cancel => S accept
        } else {
          state = NEGOTIATING;
          fprintf(stderr, "negotiating\n");

          // set IP address
          ot_ip_addr = inet_ntoa(ot_tcp_addr.sin_addr);
          if (ot_ip_addr == NULL) die("inet_ntoa", err);
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

        if (strcmp(str, "call") == 0) {
          // get ip address and port
          ot_ip_addr = strtok(NULL, " ");
          if (ot_ip_addr == NULL) die("argument (ip addr)", err);
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (tcp port)", err);
          ot_tcp_port = atoi(str);
          str = strtok(NULL, " ");
          if (str == NULL) die("argument (udp port)", err);
          ot_udp_port = atoi(str);

          // address settings
          ret = inet_aton(ot_ip_addr, &ot_tcp_addr.sin_addr);
          if (ret == 0) die("inet", err);
          ot_tcp_addr.sin_port = htons(ot_tcp_port);
          ret = inet_aton(ot_ip_addr, &ot_udp_addr.sin_addr);
          if (ret == 0) die("inet", err);
          ot_udp_addr.sin_port = htons(ot_udp_port);

          // TCP connection
          tcp_s = socket(PF_INET, SOCK_STREAM, 0);
          ret = connect(tcp_s, (struct sockaddr *)&ot_tcp_addr, sizeof(ot_tcp_addr));
          if (ret == -1) die("connect", err);

          // send INVITE message (format: "INVITE <my UDP port>")
          sprintf(cbuf, "INVITE %d", my_udp_port);
          ret = send(tcp_s, cbuf, strlen(cbuf) + 1, 0);
          if (ret == -1) die("send", err);

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
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        char *str = strtok(cbuf, " ");
        if (strcmp(str, "INVITE") == 0) {
          str = strtok(NULL, " ");    // decode the other's UDP port
          if (str == NULL) die("argument (ot udp port)", err);
          ot_udp_port = atoi(str);
          ot_udp_addr.sin_port = htons(ot_udp_port);

          state = RINGING;
          fprintf(stderr, "answer ? ");
        }
      }
    }

    else if (state == INVITING) {   // waiting for answer
      if (FD_ISSET(tcp_s, &rfds)) {  // TCP socket
        ret = recv(tcp_s, cbuf, CHAR_BUF, MSG_DONTWAIT);
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        if (strcmp(cbuf, "OK") == 0) {
          open_rec(&audioStream, &inputParameters, &udp_data);
          err = Pa_StartStream(audioStream);
          if (err != paNoError) die(NULL, err);

          state = SPEAKING;
          fprintf(stderr, "speaking\n");
          continue;
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
          if (ret == -1) die("send", err);

          open_rec(&audioStream, &inputParameters, &udp_data);
          err = Pa_StartStream(audioStream);
          if (err != paNoError) die(NULL, err);

          state = SPEAKING;
          fprintf(stderr, "speaking\n");
          continue;
        }
      }
    }

    else if (state == SPEAKING) {  // speaking
      if (FD_ISSET(udp_sock, &rfds)) {  // UDP socket
        ret = recvfrom(udp_sock, sbuf, SOUND_BUF, MSG_DONTWAIT, (struct sockaddr *)&ot_udp_addr, &ot_addr_len);
        if (ret == -1 && errno != EAGAIN) die("recv", err);
        ret = write(1, sbuf, ret);
      } else if (FD_ISSET(0, &rfds)) {  // stdin
        fgets(cbuf, CHAR_BUF, stdin);
        cbuf[strlen(cbuf) - 1] = '\0';  // delete LF
        char *str = strtok(cbuf, " ");

        if (strcmp(str, "quit") == 0) {
          err = Pa_CloseStream(audioStream);
          if(err != paNoError) die(NULL, err);
          state = NO_SESSION;
          break;
        }
      }
    }
  }
  
  done(err);

  return 0;
}
