#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef WINDOWS
#include <windows.h>
#include <mmsystem.h>
#endif

#include <inttypes.h>
#include "midi.h"
#include "logging.h"

// 55: midi command 168
// 56 : mididuino 1.0
uint8_t deviceID = 56;

midi_ack_callback_t midi_ack_callback = NULL;

#define CMD_DATA_BLOCK          0x01
#define CMD_DATA_BLOCK_ACK      0x02
#define CMD_FIRMWARE_CHECKSUM   0x03

#define SYSEX_VENDOR_1 0x00
#define SYSEX_VENDOR_2 0x13

int exitMainLoop = 0;

int statusMessage = 0;
int verbose = 1;
int convertHexFile = 0;

FILE *fin = NULL;
int canSendSysex = 1;
int firmwareChecksumSent = 0;

int waitingForBootloader = 0;

unsigned char bootmsg[] = { 0xF0, 0x00, 0x13, 0x37, 0x05, 0xF7 };

#define MAX_KB_SIZE 256 /* should be enough for now */
unsigned char flashram[MAX_KB_SIZE * 1024];
unsigned int max_address = 0;
unsigned int cur_address = 0;

void closeFile() {
  if (fin != NULL) {
    fclose(fin);
    fin = NULL; 
  }
}

int isHexFile(char *str) {
  int len = strlen(str);
  if (len > 4) {
    if (!strncmp(str + (len - 4), ".hex", 4)) {
      return 1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}


void loadHexFile(void) {
  char buf[128];
  unsigned long i;
  for (i = 0; i < sizeof(flashram); i++)
    flashram[i] = 0xFF;

  max_address = 0;
  cur_address = 0;
  
  while (fgets(buf, sizeof(buf), fin)) {
    unsigned int len = strlen(buf);
    if (len < 10) {
      logPrintf(LOG_ERROR, "wrong ihex length, should be at least 10: %s\n", buf);
      exit(1);
    }

    unsigned long address;
  
    char size_str[3];
    size_str[2] = 0;
    size_str[0] = buf[1];
    size_str[1] = buf[2];
    unsigned int size;
    sscanf(size_str, "%x", &size);
  
    char address_str[5];
    address_str[4] = 0;
    address_str[0] = buf[3];
    address_str[1] = buf[4];
    address_str[2] = buf[5];
    address_str[3] = buf[6];
    sscanf(address_str, "%lx", &address);
    
    char type_str[3];
    type_str[2] = 0;
    type_str[0] = buf[7];
    type_str[1] = buf[8];
    unsigned int type;
    sscanf(type_str, "%x", &type);

    static unsigned int i = 0;
    
    if (type == 0x00) {
      unsigned int cnt = 0;
      for (i = 0; i < size * 2; i+=2) {
	char data_str[3];
	data_str[2] = 0;
	data_str[0] = buf[9 + i];
	data_str[1] = buf[10 + i];

	unsigned int byte;
	sscanf(data_str, "%x", &byte);
	flashram[address + cnt++] = byte & 0xFF;
      }
      //      printf("address: %x, size: %x, type: %x\n", address, size, type);
      //      hexdump](flashram + address, size);
      max_address = address + cnt;
    }
  }

	uint16_t checksum = 0;
	for (i = 0; i < max_address; i++) {
		checksum += flashram[i];
	}
	debugPrintf(1, "firmware length: %x, checksum: %x\n", max_address, checksum & 0x3FFF);
  closeFile();
}


int send_sysex_bootload(void) {
  canSendSysex = 0;
  waitingForBootloader = 1;
  bootmsg[3] = deviceID;
  midiSendLong(bootmsg, 6);
  return 0;
}



int getNextSysexPart(unsigned char *outbuf, unsigned int maxSize) {
  uint8_t hdr_data[4];
  hdr_data[0] = 0xF0;
  hdr_data[1] = SYSEX_VENDOR_1;
  hdr_data[2] = SYSEX_VENDOR_2;
  hdr_data[3] = deviceID;

  if (convertHexFile) {
    unsigned int idx = 0;
    unsigned char len = 64;

    if (firmwareChecksumSent == 1) {
      return 0;
    }

    if (cur_address >= max_address) {
      unsigned int i;
      unsigned short firmware_len = 0;
      unsigned short firmware_checksum = 0;
      for (i = 0; i < max_address; i++) {
	firmware_checksum += flashram[i];
      }
      firmware_len = max_address;

      memcpy(outbuf + idx, hdr_data, 4);
      outbuf[idx + 3] = deviceID;
      idx += 4;
      unsigned char cmd = CMD_FIRMWARE_CHECKSUM;
      outbuf[idx++] = cmd;
      unsigned char b = firmware_len & 0x7F;
      outbuf[idx++] = b;
      b = (firmware_len >> 7) & 0x7F;
      outbuf[idx++] = b;
      b = (firmware_len >> 14) & 0x7F;
      outbuf[idx++] = b;
      b = firmware_checksum & 0x7F;
      outbuf[idx++] = b;
      b = (firmware_checksum >> 7) & 0x7f;
      outbuf[idx++] = b;
      outbuf[idx++] = '\xf7';

      if (verbose >= 2) {
	logPrintf(LOG_INFO, "len: %x (%x %x), checksum: %x (%x %x) \n",
		  firmware_len, (firmware_len & 0x7F), (firmware_len >> 7) & 0x7F,
		  firmware_checksum, (firmware_checksum & 0x7F), (firmware_checksum >> 7) & 0x7F);
      }
      

      firmwareChecksumSent = 1;
      return idx;
    }
    
    memcpy(outbuf + idx, hdr_data, 4);
    outbuf[idx + 3] = deviceID;
    idx += 4;
    unsigned char cmd =  CMD_DATA_BLOCK;
    unsigned char checksum = cmd;
    outbuf[idx++] = cmd;
    checksum ^= len;
    outbuf[idx++] = len;

    unsigned int address = cur_address;
    unsigned char address_bytes[4];
    address_bytes[0] = address & 0x7F;
    address >>= 7;
    address_bytes[1] = address & 0x7F;
    address >>= 7;
    address_bytes[2] = address & 0x7F;
    address >>= 7;
    address_bytes[3] = address & 0x7F;
    memcpy(outbuf + idx, address_bytes, 4);
    idx += 4;
    checksum ^= address_bytes[0];
    checksum ^= address_bytes[1];
    checksum ^= address_bytes[2];
    checksum ^= address_bytes[3];

    unsigned char *buf = flashram + cur_address;
    
    unsigned char i;
    unsigned char bits = 0;
    unsigned char tmpbuf[8];
    for (i = 0; i < len; i++) {
      unsigned char c = buf[i] & 0x7F;
      unsigned char msb = buf[i] >> 7;
      bits |= msb << (i % 7);
      tmpbuf[i % 7] = c;
      
      if ((i % 7) == 6) {
	checksum ^= bits;
	outbuf[idx++] = bits;
	bits = 0;
	memcpy(outbuf + idx, tmpbuf, 7);
	idx += 7;
      }
      checksum ^= c;
    }
    if ((i % 7) > 0) {
      checksum ^= bits;
      outbuf[idx++] = bits;
      bits = 0;
      memcpy(outbuf + idx, tmpbuf, i % 7);
      idx += i % 7;
    }
    checksum &= 0x7f;
    outbuf[idx++] = checksum;
    outbuf[idx++] = '\xf7';
    cur_address += len;

    return idx;
  } else {
    unsigned int i = 0;
    for (i = 0; i < maxSize; i++) {
      int c = fgetc(fin);
      if (c < 0) {
	return 0;
      }
      outbuf[i] = c;
      if (c == 0xf7) {
	i++;
	break;
      }
    }
    return i;
  }
}

unsigned char part_buf[512];

uint16_t make_word(uint8_t *data, uint8_t cnt) {
  int8_t idx = 0;
  int8_t ptr = idx + cnt - 1;
  uint32_t ret = 0;
  for (; ptr >= idx; ptr--) {
    ret <<= 7;
    ret |= data[ptr];
  }
  return ret;
}

int send_sysex_part(void) {
  unsigned int len = getNextSysexPart(part_buf, sizeof(part_buf));
  if (len <= 0) 
    return 0;

  if (statusMessage) {
    if (part_buf[4] == CMD_DATA_BLOCK) {
      uint16_t address = make_word(part_buf + 6, 4);
      float percent = (float)address/(float)max_address * 100.0;
      logPrintf(LOG_PROGRESS, "%2.2f %%, sending %d/%d\n", percent, address, max_address);
    }
  }
	
  if (verbose >= 2) {
    if (part_buf[4] == CMD_DATA_BLOCK) {
      uint16_t address = make_word(part_buf + 6, 4);
      logPrintf(LOG_INFO, "address: %x\n", address);
      if (verbose >= 3) {
	logPrintf(LOG_INFO, "code: \n");
	unsigned char code[512];
	unsigned int code_len = 0;
	unsigned int cnt;
	uint8_t bits;
	
	for (cnt = 0; cnt < (len - 12); cnt++) {
	  uint8_t byte = part_buf[10 + cnt];
	  
	  if ((cnt % 8) == 0) {
	    bits = byte;
	  } else {
	    code[code_len++] = byte | ((bits & 1) << 7);
	    bits >>= 1;
	  }
	}
	
	logHexdump(LOG_INFO, code, code_len);
      }
    }
    
    if (verbose >= 4) {
      logPrintf(LOG_INFO, "sysex: \n");
      logHexdump(LOG_INFO, part_buf, len);
    }
  }
  canSendSysex = 0;
  midiSendLong(part_buf, len);

  return 1;
}

typedef enum midi_stm_state_e {
  midi_wait_start = 0,
  midi_wait_vendor1,
  midi_wait_vendor2,
  midi_wait_vendor3,
  midi_wait_cmd,
  midi_wait_end
} midi_stm_state_t;

midi_stm_state_t stm_state = midi_wait_start;
unsigned char stm_cmd = 0;

void midi_sysex_cmd_recvd(unsigned char cmd) {
  if (cmd == 2) {
    if (verbose >= 2) {
      logPrintf(LOG_INFO, "ACK received\n");
    }

    if (midi_ack_callback != NULL) {
      midi_ack_callback(NULL);
    }
    
    if (waitingForBootloader) {
#ifdef WINDOWS
      Sleep(1500);
#else
      usleep(1500000);
#endif
      waitingForBootloader = 0;
    }
    if (!send_sysex_part()) {
      if (verbose >= 1) {
	logPrintf(LOG_INFO, "booting to main program\n");
      }
      if (statusMessage) {
	logPrintf(LOG_STATUS, "booting to main program\n");
      }
      
      static unsigned char buf[6] = {0xf0, 0x00, 0x13, 0x37, 0x04, 0xf7 };
      buf[3] = deviceID;
      midiSendLong(buf, 6);
#ifdef WINDOWS
      Sleep(5);
#else
      usleep(5000);
#endif
      exitMainLoop = 1;
    }
  } else {
    logPrintf(LOG_ERROR, "unknown cmd %d received\n", cmd);
  }
}

void midiReceive(unsigned char c) {
  switch (stm_state) {
  case midi_wait_start:
    if (c == 0xF0)
      stm_state = midi_wait_vendor1;
    break;

  case midi_wait_vendor1:
    if (c == SYSEX_VENDOR_1)
      stm_state = midi_wait_vendor2;
    else
      stm_state = midi_wait_start;
    break;

  case midi_wait_vendor2:
    if (c == SYSEX_VENDOR_2)
      stm_state = midi_wait_vendor3;
    else
      stm_state = midi_wait_start;
    break;

  case midi_wait_vendor3:
    if (c == deviceID)
      stm_state = midi_wait_cmd;
    else
      stm_state = midi_wait_start;
    break;

  case midi_wait_cmd:
    stm_cmd = c;
    stm_state = midi_wait_end;
    break;

  case midi_wait_end:
    if (c == 0xF7)
      midi_sysex_cmd_recvd(stm_cmd);
    stm_state = midi_wait_start;
    break;

  default:
    stm_state = midi_wait_start;
    break;
  }
}

void midiTimeout(void) {
  logPrintf(LOG_ERROR, "Midi timeout: Device is not responding\n");
  closeFile();

  exit(1);
}

void usage(void) {
  logPrintf(LOG_USAGE, "./midi-send [-l] [-h] [-s] [-b] [-v] [-d] [-I ID in hex (default 38)] -i inputDevice -o outputDevice file\n");
}

int main(int argc, char *argv[]) {
  int c;
  char outputDevice[256] = "";
  char inputDevice[256]  = "";
  int bootloader = 0;

  while ((c = getopt(argc, argv, "ho:l:i:I:bvqsd")) != -1) {
    switch (c) {
    case 'b':
      bootloader = 1;
      break;
    case 'o':
      strncpy(outputDevice, optarg, sizeof(outputDevice));
      break;

    case 'i':
      strncpy(inputDevice, optarg, sizeof(inputDevice));
      break;

    case 'd':
      debugLevel++;
      break;

    case 'I':
      if ((strlen(optarg) > 2) && optarg[0] == '0' && optarg[1] == 'x') {
	deviceID = strtol(optarg + 2, NULL, 16);
      } else {
	deviceID = strtol(optarg, NULL, 10);
      }
      logPrintf(LOG_INFO, "deviceID: %x\n", deviceID);
      break;

    case 'l':
      if (optarg[0] == 'i') {
	listInputMidiDevices();
      } else {
	listOutputMidiDevices();
      }
      exit(0);
      break;

    case 'v':
      verbose++;
      break;

    case 's':
      statusMessage = 1;
      break;

    case 'q':
      verbose--;

    case 'h':
    default:
      usage();
      exit(0);
      break;
    }
  }

  if (optind >= argc) {
    usage();
    exit(1);
  }

  char *inputFile = argv[optind];
  fin = fopen(inputFile, "r");
  if (fin == NULL) {
    logPrintf(LOG_ERROR, "Could not open file %s\n", argv[optind]);
    exit(1);
  }

  logPrintf(LOG_INFO, "input: %s\n", inputFile);
  if (isHexFile(inputFile)) {
    logPrintf(LOG_INFO, "convert hex file\n");
    loadHexFile();
    convertHexFile = 1;
  }
  
  if (!strcmp(outputDevice, "") || !strcmp(inputDevice, "")) {
    usage();
    exit(1);
  }

  midiInitialize(inputDevice, outputDevice);

  if (bootloader) {
    if (statusMessage) {
      logPrintf(LOG_STATUS, "starting bootloader");
    }
		
    send_sysex_bootload();
  } else {
    send_sysex_part();
  }

  midiMainLoop();

  closeFile();

  return 0;
}
