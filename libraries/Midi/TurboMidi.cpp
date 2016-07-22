#include "WProgram.h"

#include "Midi.h"
#include "MidiSysex.hh"
#include "TurboMidi.hh"

#ifndef HOST_MIDIDUINO

static uint8_t turbomidi_sysex_header_hack[] = {
0xF0, 0x00, 0x20, 0x3c, 0x04, 0x00
};

static uint8_t turbomidi_sysex_header[] = {
	0xF0, 0x00, 0x20, 0x3c, 0x00, 0x00
};

TurboMidiSysexListenerClass::TurboMidiSysexListenerClass() :
	MidiSysexListenerClass() {
	ids[0] = 0x00;
	ids[1] = 0x20;
	ids[2] = 0x3c;
	currentSpeed = TURBOMIDI_SPEED_1x;
	state = tm_state_normal;
}

void TurboMidiSysexListenerClass::handleByte(uint8_t byte) {
	if (MidiSysex.len == 3) {
		if (byte == 0x00) {
			isGenericMessage = true;
		} else {
			isGenericMessage = false;
		}
	}
}
static void sendTurbomidiHeaderHack(uint8_t cmd) {
    MidiUart.puts(turbomidi_sysex_header_hack, sizeof(turbomidi_sysex_header_hack));
    MidiUart.putc(cmd);
}

static void sendTurbomidiHeader(uint8_t cmd) {
    MidiUart.puts(turbomidi_sysex_header, sizeof(turbomidi_sysex_header));
    MidiUart.putc(cmd);
}

static void sendSpeedPattern() {
    for (uint8_t i = 0; i < 4; i++) {
        MidiUart.putc(0x55);
    }
    for (uint8_t i = 0; i < 4; i++) {
        MidiUart.putc(0x00);
    }
}



void TurboMidiSysexListenerClass::end() {

        //	if (!isGenericMessage)
//		return;

	switch (MidiSysex.data[5]) {
		/* master requests (when we are slave) */
	case TURBOMIDI_SPEED_REQUEST:
        //sendTurbomidiHeader(TURBOMIDI_SPEED_ANSWER);
        //MidiUart.putc(speeds);
        //MidiUart.putc(0x00);
        //MidiUart.putc(certifiedSpeeds);
        //MidiUart.putc(0x00);
        //MidiUart.putc(0xF7);        

            sendSpeedAnswer();
        break;

	case TURBOMIDI_SPEED_NEGOTIATION_MASTER:
        slave_speed1 = MidiSysex.data[6];
        slave_speed2 = MidiSysex.data[7];
        
        sendTurbomidiHeader(0x13);
        MidiUart.putc(0xF7);
        GUI.flash_printf("ACK %b %b", slave_speed1, slave_speed2);
//        setSpeed(slave_speed1); 
        setSpeed(slave_speed1); 
        
        break;

	case TURBOMIDI_SPEED_TEST_MASTER:
setLed();

        sendTurbomidiHeader(TURBOMIDI_SPEED_RESULT_SLAVE);
        
        sendSpeedPattern();
        MidiUart.putc(0xF7);
		
        break;

	case TURBOMIDI_SPEED_TEST_MASTER_2:
   setLed2();
   
        sendTurbomidiHeader(TURBOMIDI_SPEED_RESULT_SLAVE_2);
        MidiUart.putc(0xF7);
		setSpeed(slave_speed2);
        break;


		/* slave answers (when we are master) */
	case TURBOMIDI_SPEED_ANSWER:
		{
	//		if (state == tm_master_wait_req_answer) {
    GUI.flash_printf("SPEEDANS %b %b",  MidiSysex.data[6] ,  MidiSysex.data[7] );
                slaveSpeeds = MidiSysex.data[6] | (MidiSysex.data[7] << 7);
				certifiedSlaveSpeeds = MidiSysex.data[8] | (MidiSysex.data[9] << 7);
				state = tm_master_req_answer_recvd;
	//		}
		}
		break;

	case TURBOMIDI_SPEED_ACK_SLAVE:
//		if (state == tm_master_wait_speed_ack) {
			state = tm_master_speed_ack_recvd;
//		}
		break;

	case TURBOMIDI_SPEED_RESULT_SLAVE:
	//	if (state == tm_master_wait_test_1) {
			state = tm_master_test_1_recvd;
	//	}
		break;
	case TURBOMIDI_SPEED_RESULT_SLAVE_2:
	//	if (state == tm_master_wait_test_2) {
			state = tm_master_test_2_recvd;
	//	}
		break;

	default:
		break;
	}
}

static uint8_t getHighestBit(uint16_t b) {
	for (int8_t i = 15; i >= 0; i--) {
		if (IS_BIT_SET(b, i)) {
			return i;
		}
	}
	return 0;
}

void TurboMidiSysexListenerClass::stopTurboMidi() {
	setSpeed(0);
	state = tm_state_normal;
}


bool TurboMidiSysexListenerClass::startTurboMidi() {
	
//sendTurbomidiHeaderHack(0x03);
//MidiUart.putc(0x07);
//MidiUart.putc(0xF7);
//setSpeed(7);
sendTurbomidiHeader(0x20);
    MidiUart.putc(7 );
            MidiUart.putc(0xF7);
                    delay(10);
// setSpeed(tmSpeeds[7 ]);

                    MidiUart.setSpeed(tmSpeeds[7 ]);
                             MidiUart.setActiveSenseTimer(290);

   return true;
  //  return true;    
    slaveSpeeds = 0;
	certifiedSlaveSpeeds = 0;

	MidiUart.setSpeed(31250);

	uint8_t speed1;
	uint8_t speed2;
    sendTurbomidiHeaderHack(07);
    sendSpeedRequest();
	bool ret = blockForState(tm_master_req_answer_recvd);
	GUI.setLine(GUI.LINE2);

//	return true;
            if (ret) {
//		GUI.flash_printf("s %X c %X", slaveSpeeds, certifiedSlaveSpeeds);
	
    } else {
		GUI.flash_printf("REQ TIMEOUT");
		goto fail;
	}

	speed1 = getHighestBit(speeds & slaveSpeeds) + 1;
	speed2 = getHighestBit(certifiedSpeeds & certifiedSlaveSpeeds) + 1;
    speed1 = 7;
    speed2 = 7;

    sendSpeedNegotiationRequest(speed1, speed2);
	
    ret = blockForState(tm_master_speed_ack_recvd);
	GUI.setLine(GUI.LINE2);
	if (ret) {
		GUI.flash_printf("ACK %b %b", speed1, speed2);
	} else {
		GUI.flash_printf("ACK TIMEOUT");
	    return true;
	}
//    uint32_t acpu = 4;
   // UCSR0A = 1;
     // (mcpu & 0xFF);
setSpeed(speed1);
state = tm_master_wait_test_1;

sendSpeedTest1(speed1);
//sendSpeedTest1(speed1);

      //  while (tm_master_test_1_recvd != state) {
//delay(5);
  //  handleIncomingMidi();

    //   }
    
    ret = blockForState(tm_master_test_1_recvd);
	GUI.setLine(GUI.LINE2);
    if (ret) {
		GUI.flash_printf("TEST1 ACK");
	} else {
		GUI.flash_printf("TEST1 TIMEOUT");
	//	goto fail;
	}
//    for (uint8_t loop = 0; loop < 100; loop++) {
  //  midiSpeedPush(7);
   // }
   // speed1 = 7;
   // speed2 = 7;

  //  sendSpeedNegotiationRequest(speed1, speed2);
  //  MidiUart.setSpeed(31250);

    // ret = blockForState(tm_master_speed_ack_recvd);
   // GUI.setLine(GUI.LINE2);
   // if (ret) {
   //  GUI.flash_printf("ACK2 %b %b", speed1, speed2);
//} else {
  //      GUI.flash_printf("ACK2 TIMEOUT");
    //    goto fail;
    //}

    //return true;
	sendSpeedTest2(speed2);
	ret = blockForState(tm_master_test_2_recvd);
	GUI.setLine(GUI.LINE2);
    	if (ret) {
	GUI.flash_printf("TEST2 ACK");
    

    } else {
		GUI.flash_printf("TEST2 TIMEOUT");
		goto fail;
}
setSpeed(speed2);

//	MidiUart.setActiveSenseTimer(130);


	state = tm_master_ok;
	
	return true;

 fail:
	stopTurboMidi();
	return false;
}

bool TurboMidiSysexListenerClass::sendSpeedRequest() {
	state = tm_master_wait_req_answer;
	
	sendTurbomidiHeader(TURBOMIDI_SPEED_REQUEST);
	MidiUart.putc(0xF7);
}

void TurboMidiSysexListenerClass::sendSpeedNegotiationRequest(uint8_t speed1, uint8_t speed2) {
	state = tm_master_wait_speed_ack;

	sendTurbomidiHeader(TURBOMIDI_SPEED_NEGOTIATION_MASTER);
	MidiUart.putc(speed1);
//	MidiUart.putc(speed2);
	MidiUart.putc(0xF7);
}

uint32_t TurboMidiSysexListenerClass::tmSpeeds[12] = {
	31250,
	31250,
	62500,
	//104063,
    104062,
	125000,
	156250,
	208125,
	250000,
	312500,
	415625,
	500000,
	625000
};
	
void TurboMidiSysexListenerClass::setSpeed(uint8_t speed) {
	currentSpeed = speed;
    uint32_t cpu;
if (speed > 1) {
    UCSR0A |= _BV(U2X0);
cpu = (F_CPU / 8);

}
else {
cpu = (F_CPU / 16);
UCSR0A |= ~(_BV(U2X0));;

}

        cpu /= tmSpeeds[speed];
          cpu--;

            //uint32_t cpu = (F_CPU / 16);
            //cpu /= speed;
            //cpu--;
          //UBRR0H = ((cpu >> 8));
            
            UBRR0H = ((cpu >> 8) & 0xFF);
              UBRR0L = (cpu & 0xFF);
}

void TurboMidiSysexListenerClass::sendSpeedTest1(uint8_t speed1) {

	for (uint8_t i = 0; i < 16; i++) {
		MidiUart.putc(0x0C);
//        MidiUart.putc(0x00);
//            
	}
	
	sendTurbomidiHeader(TURBOMIDI_SPEED_TEST_MASTER);	
	sendSpeedPattern();
	MidiUart.putc(0xF7);
}
	
void TurboMidiSysexListenerClass::sendSpeedTest2(uint8_t speed2) {
	state = tm_master_wait_test_2;

	
	sendTurbomidiHeader(TURBOMIDI_SPEED_TEST_MASTER_2);	
	MidiUart.putc(0xF7);

}
	
void TurboMidiSysexListenerClass::sendSpeedAnswer() {
	sendTurbomidiHeader(TURBOMIDI_SPEED_ANSWER);	
//	MidiUart.putc(speeds & 0x7F);
	MidiUart.putc(32);
    MidiUart.putc(0);
    //MidiUart.putc((speeds >> 7) & 0x7F);
	MidiUart.putc(32);
    MidiUart.putc(0);

    //MidiUart.putc((certifiedSpeeds >> 7) & 0x7F);
	MidiUart.putc(0xF7);
}

void TurboMidiSysexListenerClass::midiSpeedPush(uint8_t push_speed) {
    sendTurbomidiHeader(0x20);	
    MidiUart.putc(push_speed);

    MidiUart.putc(0xF7);
}

void TurboMidiSysexListenerClass::sendSpeedNegotiationAck() {
	sendTurbomidiHeader(TURBOMIDI_SPEED_ACK_SLAVE);	
	MidiUart.putc(0xF7);
}

void TurboMidiSysexListenerClass::sendSpeedTest1Result() {
	sendTurbomidiHeader(TURBOMIDI_SPEED_RESULT_SLAVE);	
	sendSpeedPattern();
	MidiUart.putc(0xF7);
}

void TurboMidiSysexListenerClass::sendSpeedTest2Result() {
	sendTurbomidiHeader(TURBOMIDI_SPEED_RESULT_SLAVE_2);	
	MidiUart.putc(0xF7);
}

bool TurboMidiSysexListenerClass::blockForState(tm_state_t _state, uint16_t timeout) {
  uint16_t start_clock = read_slowclock();
  uint16_t current_clock = start_clock;
  do {
    current_clock = read_slowclock();
    handleIncomingMidi();
		GUI.display();
  } while ((clock_diff(start_clock, current_clock) < timeout) && (state != _state));
	return (state == _state);
}

TurboMidiSysexListenerClass TurboMidi;

#endif
