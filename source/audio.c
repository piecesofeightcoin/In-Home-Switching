#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <inttypes.h>
#include <switch.h>

#include <netdb.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <unistd.h>

#define SAMPLERATE 48000
#define CHANNELCOUNT 2
#define FRAMERATE (1000 / 30)
#define SAMPLECOUNT (SAMPLERATE / FRAMERATE)
#define BYTESPERSAMPLE 2

void diep(char *s)
{
    perror(s);
    while(1);
}

int setup_socket()
{
    struct sockaddr_in si_me, si_other;
    int s;

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *)&si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(2224);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, &si_me, sizeof(si_me)) == -1)
        diep("bind");
    return s;
}

#define BUF_COUNT 5
AudioOutBuffer audiobuf[BUF_COUNT];
u8 *buf_data[BUF_COUNT];
int curBuf = 0;
#define swapbuf (curBuf = (curBuf + 1) % (BUF_COUNT))

AudioOutBuffer *audout_released_buf;
int audout_filled = 0;
void play_buf(int buffer_size, int data_size)
{

    if (audout_filled >= BUF_COUNT)
    {
        u32 released_count;
        audoutWaitPlayFinish(&audout_released_buf, &released_count, U64_MAX);
    }

    audiobuf[curBuf].next = 0;
    audiobuf[curBuf].buffer = buf_data[curBuf];
    audiobuf[curBuf].buffer_size = buffer_size;
    audiobuf[curBuf].data_size = data_size;
    audiobuf[curBuf].data_offset = 0;
    audoutAppendAudioOutBuffer(&audiobuf[curBuf]);

    audout_filled++;

    swapbuf;
}

#define MIN_LEFT_SOCK 2000 // TODO: Tweak those
#define MAX_LEFT_SOCK 5000
char bs_buf[MAX_LEFT_SOCK];

// out_buf has to be in_buf_size*fact*2 big
void resample(unsigned short *in_buf, int in_buf_size, unsigned short *out_buf, int fact)
{
    int channels = 2;

    for (int i = 0; i < in_buf_size / sizeof(unsigned short); i += channels)
    {
        int out_base = i * fact;

        for (int j = 0; j < fact; j++)
        {
            for (int c = 0; c < channels; c++)
            {
                out_buf[out_base++] = in_buf[i + c];
            }
        }
    }
}

void audioHandlerLoop()
{


    u32 data_size = 1920; //(SAMPLECOUNT * CHANNELCOUNT * BYTESPERSAMPLE);

    int inrate = 16000;
    int outrate = 48000;

    int fact = outrate/inrate;
    char in_buf[data_size/fact];

    u32 buffer_size = (data_size + 0xfff) & ~0xfff;

    for (int curBuf = 0; curBuf < BUF_COUNT; curBuf++)
    {
        buf_data[curBuf] = memalign(0x1000, buffer_size);
    }

    int sock = setup_socket();
    printf("%d\n", sock);
    int played = 0;
    while (1)
    {
        struct sockaddr_in si_other;
        int slen = sizeof si_other;
        int ret = recvfrom(sock, in_buf, sizeof(in_buf), 0, &si_other, &slen);

        if (ret < 0)
        {
            perror("recv failed:");
            continue;
        }
        if (ret != sizeof(in_buf))
        {
            printf("Bad input %d\n", ret);
            
            continue;
            // This may or may not be bad

            //printf("not %d big! %d\n", data_size, ret);
            //continue;
        }

        resample((unsigned short*) in_buf, sizeof(in_buf), (unsigned short*) buf_data[curBuf], fact);
        play_buf(buffer_size, data_size);
        played++;
    }

    for (int curBuf = 0; curBuf < BUF_COUNT; curBuf++)
    {
        free(buf_data[curBuf]);
    }
}
