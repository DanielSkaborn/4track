
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>

#define MIDIDEVICE	"/dev/snd/midiC1D0"
#define MIDIDEVICE_LC1	"/dev/snd/midiC2D0"

int MIDIin_d;
int MIDIout_d;
int MIDIin_LC1_d;
int MIDIout_LC1_d;

unsigned char loopIndNotesLC1[4] = { 64, 65, 66, 67 };
unsigned char stepNotesLC1[16] = { 35, 39, 43, 47, 51, 55, 59, 63, 34, 38, 42, 46, 50, 54, 58, 62 };
unsigned char clavNotesLC1[16] = { 32, 37, 36, 41, 40, 44, 49, 48, 53, 52, 57, 56, 60, 33, 61, 45 };
unsigned char trackIndNotesLC1[4] = { 72, 73, 74, 75 };
                                           //                           dwn up  rest
unsigned char inLutNotesLC1[0x80];

volatile int runstate=0;	// 0=stop, 1=run

volatile int tempoTime = 25000;
volatile char clockDriver = 1;//0; // 0=internal 1=MIDI
volatile unsigned char trigView;
// sequencer mem
volatile unsigned char seqmemNote[65][64][4];	// [ptn][step][poly]
volatile unsigned char seqmemVelo[65][64];		// [ptn][step]
volatile unsigned char length[64];				// [ptn]
volatile unsigned char content[64];				// [ptn]

// playback regs
volatile unsigned char stepCnt[4];				// [track]
volatile unsigned char seqsel[4];
volatile unsigned char seqnxt[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
volatile unsigned char channel[4];
volatile unsigned char veloOffset[4] = {0x70, 0x70, 0x70, 0x70};
volatile unsigned char offNoteQueue[4][4];		// [track][poly]
volatile unsigned char masterStepCnt;
volatile unsigned char masterLength = 16;


// realtime rec regs
volatile unsigned char recCaptN=0;
volatile unsigned char rtRecCapt[4] = { 0xFF, 0xFF, 0xFF, 0xFF};
volatile unsigned char veloRec = 0x50;
volatile unsigned char veloRecChange = 0;

volatile unsigned char recPtn=0;
volatile unsigned char recStep=0;
volatile unsigned char recTrack = 0xFF;

volatile unsigned char editTrack = 0xFF;
volatile unsigned char selTrack = 0x00;

pthread_t MIDIrcv;
pthread_t MIDILC1rcv;
pthread_t InternalClock;

void *cmd_MIDI_rcv(void *arg);
void *cmd_MIDILC1_rcv(void *arg);
void *cmd_InternalClock(void *arg);

int MIDIin(unsigned char *data) {
	return read(MIDIin_d, data, 1 );
}
void MIDIout(unsigned char outbuf) {
	write(MIDIout_d, &outbuf, 1);
	return;
}
int MIDILC1in(unsigned char *data) {
	return read(MIDIin_LC1_d, data, 1 );
}
void MIDILC1out(unsigned char outbuf) {
	write(MIDIout_LC1_d, &outbuf, 1);
	return;
}

#include "Indications.c"

unsigned char applyVeloOff(v, off) {
	unsigned char temp;
	if (off > 0x70)
		temp = v + (off -0x70);
	else
		if ((0x70-off) < v)
			temp = v - (0x70-off);
		else
			temp = 1;
	if (temp > 0x7f)
		temp = 0x7F;
	return temp;
}


volatile unsigned char indicationStep = 32;
volatile unsigned char indicationStepCol = 0x3;

void PlayStart(void) {
	int t, p, i, ind=0;

	// masterStep
	if (recTrack != 0xFF) {		
		for (i=0;i<16;i++) { // make sure all ledoff step indication
			MIDILC1out(0x97);	
			MIDILC1out(stepNotesLC1[i]);
			MIDILC1out(0x7F);
		}
		MIDILC1out(0x97);	// ledon active step indication
		MIDILC1out(stepNotesLC1[0]);
		MIDILC1out(0x1);	// green
	}

	for (i=32;i<64;i++) {
		MIDILC1out(0x97);
		MIDILC1out(i);
		MIDILC1out(0x7f);	// off
	}
	indicationStep = 32;
	
	for (t=0;t<4;t++) { // track
		stepCnt[t]=0;
				// output the sequencer notes
		if (seqmemNote[seqsel[t]][stepCnt[t]][0] != 0xFE) { // 0xFE = tie
			for (p=0;p<4;p++) { // polyphony
				if (seqmemNote[seqsel[t]][stepCnt[t]][p] < 0x80) {
					MIDIout(0x90 + channel[t]);
					MIDIout(seqmemNote[seqsel[t]][stepCnt[t]][p]);
					MIDIout(applyVeloOff(seqmemVelo[seqsel[t]][stepCnt[t]], veloOffset[t]));
					offNoteQueue[t][p]= seqmemNote[seqsel[t]][stepCnt[t]][p];
					ind++;
				}
			}
			if (ind!=0) {
				MIDILC1out(0x97); // ledon active step indication
				MIDILC1out(indicationStep);
				MIDILC1out(indicationStepCol);
			} else {
				MIDILC1out(0x97); // ledoff active step indication
				MIDILC1out(indicationStep);
				MIDILC1out(0x7F);
			}
			
			ind = 0;
		}
		indicationStep++;
	}
	indicationKnobClocks();
}

void PlayStep(void) {
	int t, p, i, ind=0;

	// masterStep
	if (recTrack != 0xFF) {
		MIDILC1out(0x97);	// ledoff last step indication
		MIDILC1out(stepNotesLC1[masterStepCnt]);
		MIDILC1out(0x7F);
	}
	masterStepCnt++;
	if (masterLength == masterStepCnt) {
		masterStepCnt = 0;
		
		for (t=0;t<4;t++) {
			if (seqnxt[t] != 0xFF) {
				seqsel[t] = seqnxt[t];
				stepCnt[t] = 0xff;
				seqnxt[t] = 0xFF;
				
				for (p=0;p<4;p++) { // polyphony
					if (offNoteQueue[t][p]<0x80) { // Note off
						MIDIout(0x80 + channel[t]);
						MIDIout(offNoteQueue[t][p]);
						MIDIout(0);
					}
				}
			}
		}
		if ((recTrack == 0xFF) && (trigView == 0))
			indicateSeqSelections(0);
	}

	if (recTrack != 0xFF) {	
		MIDILC1out(0x97);	// ledon active step indication
		MIDILC1out(stepNotesLC1[masterStepCnt]);
		MIDILC1out(0x1);	// green
	}

	for (t=0;t<4;t++) { // track
		stepCnt[t]++;
		if (stepCnt[t]+1 > length[seqsel[t]]) {
			stepCnt[t] = 0;
/*			MIDILC1out(0x97);	// ledon active loop indication
			MIDILC1out(loopIndNotesLC1[t]);
			MIDILC1out(0x0);*/	// orange
		}
/*		if (stepCnt[t] == 1) {
			MIDILC1out(0x97);	// ledon active loop indication
			MIDILC1out(loopIndNotesLC1[t]);
			MIDILC1out(0x1);	// blue
		}*/

		// output the sequencer notes
		if (seqmemNote[seqsel[t]][stepCnt[t]][0] != 0xFE) { // 0xFE = tie
		
			
		
			for (p=0;p<4;p++) { // polyphony
				if (seqmemNote[seqsel[t]][stepCnt[t]][p] < 0x80) {
					MIDIout(0x90 + channel[t]);
					MIDIout(seqmemNote[seqsel[t]][stepCnt[t]][p]);
					MIDIout(applyVeloOff(seqmemVelo[seqsel[t]][stepCnt[t]], veloOffset[t]));					
					offNoteQueue[t][p] = seqmemNote[seqsel[t]][stepCnt[t]][p];
					ind++;
				}
			}
			if (trigView == 1)  {
				if (ind!=0) {
					MIDILC1out(0x97); // ledon active step indication
					MIDILC1out(indicationStep);
					MIDILC1out(indicationStepCol);
				} else {
					MIDILC1out(0x97); // ledoff active step indication
					MIDILC1out(indicationStep);
					MIDILC1out(0x7F);
				}
			}
			ind = 0;
		}
		indicationStep++;
		if (indicationStep == 64) {
			indicationStep = 32;
			indicationStepCol = 0x5;
		}
	}
	indicationKnobClocks();
}

void RecStep(void) {
	int p;
	if (recTrack != 0xff) {
		for(p=0;p<4;p++) {
			seqmemNote[recPtn][stepCnt[recTrack]][p] = rtRecCapt[p];
			rtRecCapt[p] = 0xFF;
			content[recPtn] = 1;
		}
	}
	recCaptN = 0;
	return;
}
void VeloRecStep(void) {
	if (recTrack != 0xff) {
		if (veloRec > 0x7f) veloRec = 0x7f;
		if (veloRec == 0) veloRec = 1;
		seqmemVelo[recPtn][stepCnt[recTrack]] = veloRec;
	}
}

void NoteOffStep(void) {
	int t, p;
	int nextStep;

	for (t=0;t<4;t++) { // track
		nextStep = stepCnt[t]+1;
		if (nextStep == length[seqsel[t]]) {
			nextStep = 0;
		}
		if (seqmemNote[seqsel[t]][nextStep][0] != 0xFE) { // comming step is not a tie
			for (p=0;p<4;p++) { // polyphony
				if (offNoteQueue[t][p]<0x80) { // Note off
					MIDIout(0x80 + channel[t]);
					MIDIout(offNoteQueue[t][p]);
					MIDIout(0);
				}
			}
		}
	}
}
void NoteOffStop(void) {
	int t, p;
	
	for (t=0;t<4;t++) { // track
		for (p=0;p<4;p++) { // polyphony
			if (offNoteQueue[t][p]<0x80) { // Note off
				MIDIout(0x80 + channel[t]);
				MIDIout(offNoteQueue[t][p]);
				MIDIout(0);
			}
		}
	}
}

void *cmd_InternalClock(void *arg) {
	unsigned char tickInternalClock;
	unsigned char lastRunstate = 0;
	
	while(1) {
		if (clockDriver == 0) {
			tickInternalClock++;
			if (lastRunstate == runstate)
				MIDIout(0xf8);	// MidiClock
			if (tickInternalClock == 5) {
				NoteOffStep();
			}
			if (tickInternalClock == 6) {
				tickInternalClock = 0;
				if (runstate == 1) {
					if (lastRunstate != runstate) {
						tickInternalClock = 0;
						masterStepCnt = 0;
						MIDIout(0xfa);	// Start
						PlayStart();
					} else
						PlayStep();
				} else {
					if (lastRunstate == 1)
						MIDIout(0xfc);	// Stop
					//else
						//MIDIout(0xfb);	// MidiClock
				}
				lastRunstate = runstate;
			}
			if (tickInternalClock == 3) {
				if (recCaptN != 0)
					RecStep();
				if (veloRecChange == 1) {
					VeloRecStep();
					veloRecChange = 0;
				}
			}
			tempoIndicationClock(tickInternalClock);
		}
		usleep(tempoTime);
	}
}

void *cmd_MIDI_rcv(void *arg) {
	unsigned char d=0;
	unsigned char cc=0;
	int rcvstate=0;
	int tickMidiClock=0;
	int p;
	MIDIin_d  = open(MIDIDEVICE,O_RDONLY);

//	if (clockDriver == 1)//0; // 0=internal 1=MIDI
//		printf(" - MIDI rcv drive clock\n");

	while(1) {
		if (MIDIin(&d)){
			if(d==0xF8) {

				if (runstate==1) {
					
					if (clockDriver==1) {
						
	//		printf("%d\n",tickMidiClock);
						tickMidiClock++;
						if (tickMidiClock == 5) {
							NoteOffStep();
						}
						if (tickMidiClock == 6) {
							tickMidiClock = 0;
							//printf("0\n");
							PlayStep();
						}
						if (tickMidiClock == 3) {
							if (recCaptN != 0)
								RecStep();
							if (veloRecChange == 1){
								VeloRecStep();
								veloRecChange = 0;
							}
						}
						tempoIndicationClock(tickMidiClock);
					}
				}
			}
			if(d==0xFA) { // START
				tickMidiClock = 0;
				runstate = 1;
				masterStepCnt = 0;
				printf("START\n");
//				if (clockDriver==1)
					PlayStart();
			}
			if(d==0xFB) {
				runstate = 1;
				tickMidiClock++;
				if (tickMidiClock == 6) {
					tickMidiClock = 0;
					printf("CONTINUE\n%d\n",(int)(masterStepCnt)+1);
//					if (clockDriver==1)
						PlayStep();
				}
			}
			if(d==0xFC) {
				runstate = 0;
				printf("STOP\n");
				NoteOffStop();
			}
			
			switch(rcvstate) {
			case 0:
				if ((d & 0x90)==(0x90))
					rcvstate = 1;
				break;
			case 1:
				rcvstate = 2;
				break;
			case 2:
				rcvstate = 0;
				break;
			default:
				rcvstate = 0;
				break;
			}
		}
	}
}

void *cmd_MIDILC1_rcv(void *arg) {
	unsigned char d=0;
	unsigned char cc=0;
	unsigned char cctest=0;
	unsigned char cctry=0;
	int rcvstate=0;
	int oct=3;
	int i;
	int recP=0;
	
	MIDIin_LC1_d  = open(MIDIDEVICE_LC1,O_RDONLY);

	while(1) {
		if (MIDILC1in(&d)){
			switch(rcvstate) {
			case 0:
				if ((d & 0xF0)==(0x90)) rcvstate = 1;
				if ((d & 0xF0)==(0x80)) rcvstate = 3;
				if ((d & 0xF0)==(0xB0)) rcvstate = 4; //CC msg
				break;
			case 1:
				if (runstate == 0) { // not running

					if ((d > 31) && (d< 64) && (recTrack== 0xFF)) { // sequence select
						seqnxt[selTrack] = d-32;
						MIDILC1out(0x97);
						MIDILC1out(d);
						MIDILC1out(8);
					} else { // recording steprecord
						if (inLutNotesLC1[d] < 12) { // capture a note
							seqmemNote[recPtn][recStep][recP] = inLutNotesLC1[d]+12*oct;
							recP++; if (recP==4) {recP=0;}
							if (recTrack != 0xFF) {	// monitor the tone
								MIDIout(0x90 + channel[recTrack]);
								MIDIout(inLutNotesLC1[d]+12*oct);
								MIDIout(seqmemVelo[recPtn][recStep]);
							}
						}
					
						if (inLutNotesLC1[d] == 15) { // clear note(s)
							seqmemNote[recPtn][recStep][recP] = 0xFF; // no note
							recP++; if (recP==4) recP=0;
						}
					
						if (inLutNotesLC1[d] == 12) { // capture tie
							recP=0;
							seqmemNote[recPtn][recStep][recP] = 0xFE; // tie
						}
					
						if (inLutNotesLC1[d] == 13) { // octave down
							if(oct > 0)
								oct--;
						}
						if (inLutNotesLC1[d] == 14) { // octave up
							if (oct < 6)
								oct++;
						}
						if ( (inLutNotesLC1[d] > 15) && (inLutNotesLC1[d] < 32)) {
							recStep = inLutNotesLC1[d] - 16;
							recP=0;
							indicateStep(recStep);
						}
				
						if (inLutNotesLC1[d] != 0xFF) {
//							for (i=0;i<length[recPtn];i++) {
//								printf("%02d | %02x %02x %02x %02x |\n", i+1, seqmemNote[recPtn][i][0], seqmemNote[recPtn][i][1],seqmemNote[recPtn][i][2],seqmemNote[recPtn][i][3]);
//							}
						} else {
							printf(" Unmapped LC1 NoteOn %d 0x%x\n",d,d);
						}
					}
				}
								
				if (inLutNotesLC1[d] == 36) { // edit track 1
					editTrack = 0;
					selTrack = 0;
				}
				if (inLutNotesLC1[d] == 37) { // edit track 2
					editTrack = 1;
					selTrack = 1;
				}
				if (inLutNotesLC1[d] == 38) { // edit track 3
					editTrack = 2;
					selTrack = 2;
				}
				if (inLutNotesLC1[d] == 39) { // edit track 4
					editTrack = 3;
					selTrack = 3;
				}

				if (inLutNotesLC1[d] == 32) { // track 1 rec
					if (recTrack==0) {
						recTrack = 0xFF;
						recPtn = 64; // dump ptn
						indicateSeqSelections(0);
					}
					else {
						recTrack = 0;
						recPtn = seqsel[0];
						indicationClav();
					}
					showRecTrack();
				}				
				if (inLutNotesLC1[d] == 33) { // track 2 rec
					if (recTrack==1) {
						recTrack = 0xFF;
						recPtn = 64; // dump ptn
					} else {
						recTrack = 1;
						recPtn = seqsel[1];
						indicationClav();
					}
					showRecTrack();
				}
				if (inLutNotesLC1[d] == 34) { // track 3 rec
					if (recTrack==2) {
						recTrack = 0xFF;
						recPtn = 64;
					} else {
						recTrack = 2;
						recPtn = seqsel[2];
						indicationClav();
					}
					showRecTrack();
				}
				if (inLutNotesLC1[d] == 35) { // track 4 rec
					if (recTrack==3) {
						recTrack = 0xFF;
						recPtn = 64;
					} else {
						recTrack = 3;
						recPtn = seqsel[3];
						indicationClav();
					}
					showRecTrack();
				}

				if (runstate == 1) { // running 
					if ((d > 31) && (d< 64) && (recTrack== 0xFF)) { // sequence select
						seqnxt[selTrack] = d-32;
						MIDILC1out(0x97);
						MIDILC1out(d);
						MIDILC1out(2); // red green blink
					} else { // realtime record
						if (inLutNotesLC1[d] < 12) { // note
							if (recCaptN<4) {
								rtRecCapt[recCaptN] = inLutNotesLC1[d]+12*oct;
								recCaptN++;
							}
						}
						if (inLutNotesLC1[d] == 12) { // tie
							rtRecCapt[0] = 0xFE; 
							rtRecCapt[1] = 0xFE; 
							rtRecCapt[2] = 0xFE; 
							rtRecCapt[3] = 0xFE; 
							recCaptN=1;
						}
						if (inLutNotesLC1[d] == 15) { // no note
							if (recCaptN<4) {
								rtRecCapt[recCaptN] = 0xFF; 
								recCaptN++;
							}
						}
						if (inLutNotesLC1[d] == 13) {
							if(oct > 0)
								oct--;
						}
						if (inLutNotesLC1[d] == 14) {
							if (oct < 6)
								oct++;
						}
					}
				}
				rcvstate = 0;
				break;
			case 2:
				rcvstate = 0;
				break;
			case 3: // note off
				if (runstate == 0) {
					if (inLutNotesLC1[d] < 12) {
						if (recTrack!=0xFF) {
							MIDIout(0x80 + channel[recTrack]);	// note off
							MIDIout(inLutNotesLC1[d]+12*oct);
							MIDIout(0);
						}
					}
				}
				if ((inLutNotesLC1[d] > 35) && (inLutNotesLC1[d] < 40)) { // edit no track
					editTrack = 0xFF;
					if (recTrack == 0xFF) {
						indicateSeqSelections(0);
					}
				}
				
				rcvstate = 0;
				break;
			case 4: // midi CC control no
				cc = d;
				rcvstate = 5;
				break;
				
			case 5: // midi CC control data
				if (editTrack != 0xFF) {
					if (cc==0x10) { // length edit
						if (d==0x41) // inc
							if (length[seqsel[editTrack]] < 64)
								length[seqsel[editTrack]]++;
						if (d==0x3f) // dec
							if (length[seqsel[editTrack]] > 1)
								length[seqsel[editTrack]]--;
						indicateSeqLen(length[seqsel[editTrack]]);
					}
					if (cc==0x11) { // velo offset
						if (d==0x41) // inc
							if (veloOffset[editTrack] < 0xC0)
								veloOffset[editTrack]++;
						if (d==0x3f) // dec
							if (veloOffset[editTrack] > 2)
								veloOffset[editTrack]--;
					}
					if (cc==0x12) { // MIDI channel
						if (d==0x41) // inc
							if (channel[editTrack] < 15)
								channel[editTrack]++;
						if (d==0x3f) // dec
							if (channel[editTrack] > 0)
								channel[editTrack] --;
						channelIndication(channel[editTrack]);
					}
				}
				else {
					if (cc==0x10) { // run / stop
						if (d==0x41) // inc
							if (runstate == 0)
								runstate = 1;
						if (d==0x3f) // dec
							if (runstate == 1) {
								runstate = 0;
								MIDILC1out(0xB7);
								MIDILC1out(0x10);
								MIDILC1out(0);
								MIDILC1out(0xB7);
								MIDILC1out(0x14);
								MIDILC1out(0);
								MIDILC1out(0xB7);
								MIDILC1out(0x15);
								MIDILC1out(0);
								MIDILC1out(0xB7);
								MIDILC1out(0x16);
								MIDILC1out(0);
								MIDILC1out(0xB7);
								MIDILC1out(0x17);
								MIDILC1out(0);
							}
					}
					if (cc==0x13) { // tempo
						if (d==0x41) // inc
							if (tempoTime > 5000)
								tempoTime-=250;
						if (d==0x3f) // dec
							if (tempoTime < 100000)
								tempoTime+=250;
					}
				}
				if (recTrack != 0xFF) { // record
					if (cc==0x11) { // rec velo
						veloRecChange = 1;
						if (d==0x41) // inc
							if (veloRec < 0x77)
								veloRec+= 0x08;
						if (d==0x3f) // dec
							if (veloRec > 0x08)
								veloRec-= 0x08;
						if (veloRec>0x7f)
							veloRec = 0x7F;
					}
				}
				
				rcvstate = 0;
				break;
			default:
				rcvstate = 0;
				break;
			}
		}
	}
}

void clearSequence(int ptn) {
	int s, p;
	for (s=0;s<64;s++) {
		seqmemVelo[ptn][s] = 0x50;
		for (p=0;p<4;p++) {
			seqmemNote[ptn][s][p] = 0xFF;	// [ptn][step][poly]
		}
	}
	content[ptn] = 0;
	length[ptn]=16;
}

int main(void) {
	unsigned char rec_ch = 0;
	int i;

	printf("\nFourTrack\n\n");

	for (i=0;i<0x80;i++)
		inLutNotesLC1[i] = 0xFF;
	for (i=0;i<13;i++)
		inLutNotesLC1[clavNotesLC1[i]] = i;
	for (i=0;i<16;i++)
		inLutNotesLC1[stepNotesLC1[i]] = i + 16;

	inLutNotesLC1[clavNotesLC1[13]] = 13; // oct down
	inLutNotesLC1[clavNotesLC1[14]] = 14; // oct up
	inLutNotesLC1[clavNotesLC1[15]] = 15; // rest

	inLutNotesLC1[trackIndNotesLC1[0]] = 32; // track 1 rec
	inLutNotesLC1[trackIndNotesLC1[1]] = 33; // track 2 rec
	inLutNotesLC1[trackIndNotesLC1[2]] = 34; // track 3 rec
	inLutNotesLC1[trackIndNotesLC1[3]] = 35; // track 4 rec

	inLutNotesLC1[loopIndNotesLC1[0]] = 36;	// track 1 edit
	inLutNotesLC1[loopIndNotesLC1[1]] = 37;	// track 2 edit
	inLutNotesLC1[loopIndNotesLC1[2]] = 38;	// track 3 edit
	inLutNotesLC1[loopIndNotesLC1[3]] = 39;	// track 4 edit

	for (i=0;i<64;i++) {
		clearSequence(i);
	}

	channel[0]=8;
	channel[1]=0;
	channel[2]=1;
	channel[3]=2;
	seqsel[1]=1;
	seqsel[2]=2;
	seqsel[3]=3;
	length[3]=12;
	length[2]=64;
	length[0]=64;
	printf(" - seq data initialized\n");

	MIDIout_d = open(MIDIDEVICE,O_WRONLY);
	MIDIout_LC1_d = open(MIDIDEVICE_LC1,O_WRONLY);
	printf(" - devices opened\n");

	pthread_create(&MIDIrcv, NULL, cmd_MIDI_rcv, NULL);
	pthread_create(&MIDILC1rcv, NULL, cmd_MIDILC1_rcv, NULL);
	if (clockDriver == 0)
		pthread_create(&InternalClock, NULL, cmd_InternalClock, NULL);
	printf(" - threads created\n");
	if (clockDriver == 1)//0; // 0=internal 1=MIDI
		printf(" - MIDI clock\n");
	if (clockDriver == 0)//0; // 0=internal 1=MIDI
		printf(" - Internal clock\n");
	
	for (i=0;i<128;i++) {
		MIDILC1out(0x97);
		MIDILC1out(i);
		if ( ((i>15) && (i<24)) || ((i>63) && (i<76))) {
			MIDILC1out(0x01);
		} else {
			MIDILC1out(0x7F);
		}

	}
/*	for (i=0;i<12;i++) {
		MIDILC1out(0x97);
		MIDILC1out(clavNotesLC1[i]);
		MIDILC1out(0x5);
	}
	MIDILC1out(0x97);
	MIDILC1out(clavNotesLC1[12]);
	MIDILC1out(0x3);
	showRecTrack();
*/	
	MIDILC1out(0xB7);
	MIDILC1out(0x10);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x11);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x12);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x13);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x14);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x15);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x16);
	MIDILC1out(0);
	MIDILC1out(0xB7);
	MIDILC1out(0x17);
	MIDILC1out(0);
	
//	clockDriver = 0; // internal
	runstate = 0;
	printf(" - cleared LC1\n - ready to run\n\n");
	while(1) {
		sleep(1);
	}
}
