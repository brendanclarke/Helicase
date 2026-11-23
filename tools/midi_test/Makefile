# Host test for Core/MIDI/MidiNoteMap.c (S083). Host cc only.
CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRC     = ../../Core/MIDI/MidiNoteMap.c
NOTE_SRC = ../../Core/MIDI/MidiNoteModes.c

notemap_test: notemap_test.c $(SRC) ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMAP_HOST_TEST -I../../Core/MIDI \
	    -o $@ notemap_test.c $(SRC)

notemode_test: notemode_test.c $(NOTE_SRC) ../../Core/MIDI/MidiNoteModes.h \
	    notemode_host.h ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMODE_HOST_TEST -I. -I../.. -I../../Core/MIDI \
	    -o $@ notemode_test.c $(NOTE_SRC)

run: notemap_test notemode_test
	./notemap_test
	./notemode_test

clean:
	rm -f notemap_test notemode_test

.PHONY: run clean
