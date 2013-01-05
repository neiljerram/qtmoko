
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

/* Message levels. */
#define ML_LOW   0
#define ML_MED   1
#define ML_HIGH  2
#define ML_ERROR 3
#define ML_FATAL 4
#define ML_NONE  0xFF

const char *ml_names[8] = 
  {
    "LOW",
    "MED",
    "HIGH",
    "ERROR",
    "FATAL",
    "NONE",
    "NONE",
    "NONE"
  };

#define READ_MAX_RETRY 50

#define MSG_BUF_SIZE 74

int interrupted = 0;

unsigned short ccitt_table[256] = 
  {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
  };

unsigned short compute_crc(unsigned char *buf, int len)
{
  unsigned short crc = 0xffff;
  int ii;

  for (ii = 0; ii < len; ii++)
    {
      crc = (crc >> 8) ^ ccitt_table[(crc ^ buf[ii]) & 0xff];
    }

  return (crc ^ 0xffff);
}

unsigned short unpack_v(unsigned char *c)
{
  return (*c + 256 * *(c+1));
}

unsigned int unpack_V(unsigned char *c)
{
  return (*c + 256 * *(c+1) + 65536 * *(c+2) + 16777216 * *(c+3));
}

const char *read_packet(int fd, unsigned char *buf, int buf_size, int no_crc)
{
  unsigned char c;
  int len = 0;
  int tries = 0;
  int ii, jj;
  unsigned short crc_claimed;
  unsigned short crc_calculated;

  for (;;)
    {
      if (read(fd, &c, 1) != 1)
	{
	  tries++;
	  if (tries >= READ_MAX_RETRY)
	    {
	      return "Cannot read data from tty";
	    }
	  usleep(50000);
	  continue;
	}

      if (c == '~')
	{
	  break;
	}

      if (len >= buf_size)
	{
	  return "Packet too big";
	}

      buf[len] = c;
      len++;
    }

  /* For some reason, as the Ruby code says: */
  /* packet.gsub!("\x7d\x5e", "\x7e") */
  /* packet.gsub!("\x7d\x5d", "\x7d") */
  ii = 0;
  while (ii + 1 < len)
    {
      if ((buf[ii] == 0x7d) && ((buf[ii + 1] == 0x5e) ||
				(buf[ii + 1] == 0x5d)))
	{
	  buf[ii] = buf[ii + 1] + 0x20;
	  for (jj = ii + 2; jj < len; jj++)
	    {
	      buf[jj - 1] = buf[jj];
	    }
	  len--;
	}
      ii++;
    }

  if (!no_crc)
    {
      /* Check CRC. */
      crc_claimed = unpack_v(&buf[len - 2]);
      crc_calculated = compute_crc(buf, len - 2);
      if (crc_claimed != crc_calculated)
	{
	  return "Bad CRC";
	}
    }

  return NULL;
}

void set_message_level(int fd,
		       unsigned char ml,
		       unsigned char *buf,
		       int buf_size)
{
  unsigned char packet[6];
  unsigned short crc;
  int ii;
  const char *read_err;

  packet[0] = 31;		/* GET_MESSAGE */
  packet[1] = ml;
  packet[2] = 0;
  crc = compute_crc(packet, 3);
  packet[3] = crc & 0xff;
  packet[4] = crc >> 8;
  packet[5] = '~';

  for (ii = 0; ii < 5; ii++)
    {
      write(fd, packet, 6);
      read_err = read_packet(fd, buf, buf_size, 0);
      if (!read_err)
	{
	  break;
	}
    }

  if (read_err)
    {
      fprintf(stderr, "set_message_level: %s\n", read_err);
      exit(-1);
    }
}

void print_message(unsigned char *msg_buf)
{
  unsigned char msg_level;
  const char *filename;
  unsigned short line_num;
  const char *fmt_string;
  unsigned int code1, code2, code3;
  unsigned int timestamp;
 
  msg_level = msg_buf[0];
  if (msg_level == ML_LOW)
    filename = "";
  else
    filename = &msg_buf[1];
  line_num = unpack_v(&msg_buf[14]);
  fmt_string = &msg_buf[16];
  code1 = unpack_V(&msg_buf[56]);
  code2 = unpack_V(&msg_buf[60]);
  code3 = unpack_V(&msg_buf[64]);
  timestamp = unpack_V(&msg_buf[70]);

  printf("[TS %u] %s (%s:%d) ",
	 timestamp,
	 ml_names[msg_level & 7],
	 filename,
	 line_num);
  printf(fmt_string,
	 code1,
	 code2,
	 code3);
  printf("\n");
}

void handle_interrupt(int signum)
{
  interrupted = 1;
}

main(int argc, char **argv)
{
  int fd;
  int ii;
  unsigned char packet[256];
  unsigned char msg_level;
  const char *read_err;
  struct sigaction int_act = { 0 };

  int_act.sa_handler = handle_interrupt;
  int_act.sa_flags = 0;
  sigaction(SIGINT, &int_act, NULL);
 
  /* Parse options. */
  const char *usage =
    "%s [-l MSG_LEVEL] [-c MSG_COUNT] [-k BUF_SIZE]\n"
    "\n"
    " -l MSG_LEVEL   Extract messages of level MSG_LEVEL and above.  0=LOW,\n"
    "                1=MED, 2=HIGH, 3=ERROR, 4=FATAL.  (Default 0.)\n"
    "\n"
    " -c MSG_COUNT   Extract only MSG_COUNT messages, then terminate.\n"
    "\n"
    " -k BUF_SIZE    Write extracted messages into a circular memory buffer\n"
    "                with size BUF_SIZE Kb, instead of printing them out\n"
    "                immediately, and print out the contents of the memory\n"
    "                buffer on termination.\n";
  unsigned char opt_level = ML_LOW;
  unsigned int opt_count = 0;
  unsigned int opt_buf_size = 0;
  char *mem_buf = NULL;
  int mem_offset = 0;

  for (ii = 1; ii < argc; ii++)
    {
      if (!strcmp(argv[ii], "-l"))
	{
	  ii++;
	  assert(ii < argc);
	  opt_level = (unsigned char)atoi(argv[ii]);
	  printf("Message level %d\n", (int)opt_level);
	}
      else if (!strcmp(argv[ii], "-c"))
	{
	  ii++;
	  assert(ii < argc);
	  opt_count = (unsigned int)atoi(argv[ii]);
	  printf("Message count %u\n", opt_count);
	}
      else if (!strcmp(argv[ii], "-k"))
	{
	  ii++;
	  assert(ii < argc);
	  opt_buf_size = (unsigned int)atoi(argv[ii]);
	  printf("Memory buffer size %u Kb\n", opt_buf_size);
	  opt_buf_size *= 1024;
	  opt_buf_size /= MSG_BUF_SIZE;
	  opt_buf_size *= MSG_BUF_SIZE;
	  mem_buf = (char *)malloc(opt_buf_size);
	  assert(mem_buf);
	  memset(mem_buf, 0, opt_buf_size);
	  mem_buf[opt_buf_size - 1] = '\n';
	}
      else if (!strcmp(argv[ii], "-h"))
	{
	  printf(usage, argv[0]);
	  exit(0);
	}
      else
	{
	  fprintf(stderr, "ERROR: Unrecognised option '%s'\n", argv[ii]);
	  fprintf(stderr, usage, argv[0]);
	  exit(-1);
	}
    }

  /* Open /dev/ttyHS_Diagnostic. */
  fd = open("/dev/ttyHS_Diagnostic", O_RDWR);
  assert(fd >= 0);

  set_message_level(fd, opt_level, packet, sizeof(packet));

  for (ii = 0; (!interrupted) && ((opt_count == 0) || (ii < opt_count)); ii++)
    {
      read_err = read_packet(fd, packet, sizeof(packet), 1);
      if (read_err)
	{
	  fprintf(stderr, "main: %s (after %d messages)\n", read_err, ii);
	  continue;
	}

      /* quantity = unpack_v(&packet[1]); */
      /* drop_count = unpack_V(&packet[3]); */
      /* total_msgs = unpack_V(&packet[7]); */
      msg_level = packet[11];
      if (msg_level < opt_level)
	continue;

      if (mem_buf)
	{
	  if (MSG_BUF_SIZE < opt_buf_size - mem_offset)
	    {
	      memcpy(mem_buf + mem_offset, packet + 11, MSG_BUF_SIZE);
	      mem_offset += MSG_BUF_SIZE;
	    }
	  else
	    {
	      memcpy(mem_buf, packet + 11, MSG_BUF_SIZE);
	      mem_offset = MSG_BUF_SIZE;
	    }
	}
      else
	print_message(packet + 11);
    }

  set_message_level(fd, ML_NONE, packet, sizeof(packet));

  close(fd);

  if (mem_buf)
    {
      for (ii = mem_offset; ii < opt_buf_size; ii += MSG_BUF_SIZE)
	print_message(mem_buf + ii);
      for (ii = 0; ii < mem_offset; ii += MSG_BUF_SIZE)
	print_message(mem_buf + ii);
    }

  free(mem_buf);
}
