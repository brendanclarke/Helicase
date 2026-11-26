# Host test for Core/MIDI/MidiNoteMap.c (S083). Host cc only.
CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRC     = ../../Core/MIDI/MidiNoteMap.c
NOTE_SRC = ../../Core/MIDI/MidiNoteModes.c


# S104 P1: the real Omni one-track map, extracted from MidiNotes.c (law
# NM-OMNI in notemap_test.c). The break build removes `map->global = 0u;`.
omni_map.c: ../../Core/MIDI/MidiNotes.c
	python3 ../dsp_test/extract.py --src ../../Core/MIDI/MidiNotes.c \
	    --func midiNotes_omniMap --out $@

notemap_test: notemap_test.c omni_map.c $(SRC) ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMAP_HOST_TEST -I../../Core/MIDI \
	    -o $@ notemap_test.c $(SRC)

notemap_break_omni: notemap_test.c omni_map.c $(SRC) ../../Core/MIDI/MidiNoteMap.h
	mkdir -p break_omni
	cp notemap_test.c break_omni/notemap_test.c
	sed 's/\tmap->global = 0u;//' omni_map.c > break_omni/omni_map.c
	@! grep -q 'map->global = 0u;' break_omni/omni_map.c || \
	  { echo "notemap_break_omni: sed did not apply"; exit 1; }
	$(CC) $(CFLAGS) -DMIDI_NOTEMAP_HOST_TEST -I../../Core/MIDI \
	    -o $@ break_omni/notemap_test.c $(SRC)

notemode_test: notemode_test.c $(NOTE_SRC) ../../Core/MIDI/MidiNoteModes.h \
	    notemode_host.h ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMODE_HOST_TEST -I. -I../.. -I../../Core/MIDI \
	    -o $@ notemode_test.c $(NOTE_SRC)

# S103 P6: the release hold's break build. MNM_RELEASE_HOLD_US 0 (no hold)
# must fail the NM-HOLD laws; the sed is checked so a renamed constant cannot
# make the break silently pass.
notemode_break_hold: notemode_test.c $(NOTE_SRC) ../../Core/MIDI/MidiNoteModes.h \
	    notemode_host.h ../../Core/MIDI/MidiNoteMap.h
	mkdir -p break_hold
	sed 's/#define MNM_RELEASE_HOLD_US 50000u/#define MNM_RELEASE_HOLD_US 0u/' \
	    $(NOTE_SRC) > break_hold/MidiNoteModes.c
	@grep -q '#define MNM_RELEASE_HOLD_US 0u' break_hold/MidiNoteModes.c || \
	  { echo "notemode_break_hold: sed did not apply"; exit 1; }
	$(CC) $(CFLAGS) -DMIDI_NOTEMODE_HOST_TEST -I. -I../.. -I../../Core/MIDI \
	    -o $@ notemode_test.c break_hold/MidiNoteModes.c

run: notemap_test notemode_test notemode_break_hold notemap_break_omni
	./notemap_test
	./notemode_test
	@if ./notemap_break_omni >/dev/null 2>&1; then \
	  echo "notemap_break_omni: FAILED (the G stripe was not caught)"; exit 1; \
	else echo "notemap_break_omni: caught (NM-OMNI fails as expected)"; fi
	@if ./notemode_break_hold >/dev/null 2>&1; then \
	  echo "notemode_break_hold: FAILED (no release hold not caught)"; exit 1; \
	else echo "notemode_break_hold: caught (NM-HOLD fails as expected)"; fi

clean:
	rm -rf notemap_test notemode_test notemode_break_hold notemap_break_omni \
	    break_hold break_omni omni_map.c

.PHONY: run clean
