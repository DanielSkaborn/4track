void showRecTrack(void) {
	int i;
	for (i=0;i<4;i++) {
		MIDILC1out(0x97);
		MIDILC1out(trackIndNotesLC1[i]);
		if (i==recTrack) {
			MIDILC1out(0x2);
		} else {
			MIDILC1out(0x1);
		}
	}
	return;
}


void indicationKnobClocks(void) {
	unsigned char temp;
	
	// master loop step
	MIDILC1out(0xB7);
	MIDILC1out(0x10);
	MIDILC1out(0xf & (masterStepCnt+1));

	// seq loop steps
	temp = (unsigned char)((int)(16*(int)(stepCnt[0])) / (int)(length[seqsel[0]]));
	MIDILC1out(0xB7);
	MIDILC1out(0x14);
	MIDILC1out(0xf & (temp+1));

	temp = (unsigned char)((int)(16*(int)(stepCnt[1])) / (int)(length[seqsel[1]]));
	MIDILC1out(0xB7);
	MIDILC1out(0x15);
	MIDILC1out(0xf & (temp+1));

	temp = (unsigned char)((int)(16*(int)(stepCnt[2])) / (int)(length[seqsel[2]]));
	MIDILC1out(0xB7);
	MIDILC1out(0x16);
	MIDILC1out(0xf & (temp+1));

	temp = (unsigned char)((int)(16*(int)(stepCnt[3])) / (int)(length[seqsel[3]]));
	MIDILC1out(0xB7);
	MIDILC1out(0x17);
	MIDILC1out(0xf & (temp+1));
}


void tempoIndicationClock(unsigned char tick) {
	static unsigned char stepCntr;
	
	if (tick == 0) {
		stepCntr++;
		if (stepCntr == 16)
			stepCntr = 0;
		MIDILC1out(0xB7);
		MIDILC1out(0x13);
		MIDILC1out(stepCntr);
	}
	if (tick == 1) {
		MIDILC1out(0xB7);
		MIDILC1out(0x13);
		MIDILC1out(0x10);
	}
}

void channelIndication(unsigned char ch) {
	unsigned char i;
	
	for (i=0;i<32;i++) {
		if (i>ch) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x7F); // off
		} else {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x03); // pink
		}
	}
}

void indicateSeqSelections(unsigned char page) {
	unsigned char offset, i, temp;
	
	// 0x7f = off
	// 0x01 = green
	// 0x03 = pink
	// 0x05 = blue
	// 0x07 = red
	// 0x00 = red
	// 0x02 = green/red
	// 0x04 = pink/red
	// 0x06 = blue/red
	// 0x08 = off/red
	// 0x09 = off
	
	if (page == 0)
		offset = 0;
	if (page == 1)
		offset = 32;
		
	for (i=0;i<32;i++) {
		if (content[i+offset] != 0) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x01); // green
		} else {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x7F); // off
		}
		
/*		if (seqsel[0] == i+offset) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x07); // red
		}
		if (seqsel[1] == i+offset) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x07);
		}
		if (seqsel[2] == i+offset) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x07);
		}
		if (seqsel[3] == i+offset) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x07);
		}
*/		
		if (seqsel[selTrack] == i+offset) {
			MIDILC1out(0x97);
			MIDILC1out(i+32);
			MIDILC1out(0x07); // red
		}
	}
}

void indicateSeqLen(unsigned char len) {
	unsigned char temp, i;
	
	if ( (len & 0xF)==0) {
		for (i=0;i<16;i++) {
			MIDILC1out(0x97);
			MIDILC1out(stepNotesLC1[i]);
			MIDILC1out(0x01);
		}
	} else {
		for (i=0;i<16;i++) {
			MIDILC1out(0x97);
			MIDILC1out(stepNotesLC1[i]);
			if (i<((len & 0x0f)))
				MIDILC1out(0x01);
			else 
				MIDILC1out(0x7F);
		}
	}
	
	temp = ((len) & 0x30)>>4;
	for (i=0; i<((((len-1) & 0x30)>>4)+1);i++) {
		if ((len & 0x0f) < 5) {
			MIDILC1out(0x97);
			MIDILC1out(stepNotesLC1[i+8]);
			MIDILC1out(0x03);
		} else {
			MIDILC1out(0x97);
			MIDILC1out(stepNotesLC1[i]);
			MIDILC1out(0x03);

		}
	}
}

void indicationClav(void) {
	unsigned char i;
	for (i=0;i<12;i++) {
		MIDILC1out(0x97);
		MIDILC1out(clavNotesLC1[i]);
		MIDILC1out(0x5); // blue
	}
	MIDILC1out(0x97);
	MIDILC1out(clavNotesLC1[12]);
	MIDILC1out(0x3); //pink
}


void indicateStep(unsigned char step) {
	unsigned char i;
	for (i=0;i<16;i++) {
		MIDILC1out(0x97);
		MIDILC1out(stepNotesLC1[i]);
		if (i==(step & 0x0f))
			MIDILC1out(0x01);
		else
			MIDILC1out(0x7F);
	}
}
