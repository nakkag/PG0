/*
 * PG0 library
 *
 * screen_sound.c
 *
 * Square wave synthesizer (playSound / playMusic / bgm) on a waveOut mixer thread.
 */

/* Include Files */
#include <windows.h>
#include <mmsystem.h>
#include <tchar.h>
#include <math.h>

#include "screen.h"

#pragma comment(lib, "winmm.lib")

/* Define */
#define SND_RATE				44100
#define SND_FRAMES				256
#define SND_BUFFERS				6
#define SND_GAIN				0.5

/* Struct */
typedef struct _SND_TRACK {
	SC_NOTE *notes;
	int count;
	double loop_len;
	BOOL repeat;
	int group;
	__int64 start;
	struct _SND_TRACK *next;
} SND_TRACK;

/* Global Variables */
static CRITICAL_SECTION snd_cs;
static BOOL snd_cs_init = FALSE;
static volatile BOOL snd_failed = FALSE;
static HANDLE snd_thread = NULL;
static HANDLE snd_event = NULL;
static volatile LONG snd_quit = 0;
static SND_TRACK *snd_tracks = NULL;
static __int64 snd_pos = 0;
static HWAVEOUT snd_out = NULL;
static WAVEHDR snd_hdr[SND_BUFFERS];
static short snd_buf[SND_BUFFERS][SND_FRAMES];

/* Local Function Prototypes */

/*
 * snd_lock / snd_unlock
 */
static void snd_lock(void)
{
	if (!snd_cs_init) {
		InitializeCriticalSection(&snd_cs);
		snd_cs_init = TRUE;
	}
	EnterCriticalSection(&snd_cs);
}

static void snd_unlock(void)
{
	LeaveCriticalSection(&snd_cs);
}

/*
 * free_track
 */
static void free_track(SND_TRACK *track)
{
	if (track->notes != NULL) {
		HeapFree(GetProcessHeap(), 0, track->notes);
	}
	HeapFree(GetProcessHeap(), 0, track);
}

/*
 * fill_buffer - mix the active tracks into one buffer
 */
static void fill_buffer(short *buf)
{
	SND_TRACK *track, *prev, *next;
	int i, n;

	snd_lock();
	for (i = 0; i < SND_FRAMES; i++) {
		double v = 0;
		__int64 sample = snd_pos + i;
		for (track = snd_tracks; track != NULL; track = track->next) {
			double t = (double)(sample - track->start) * 1000.0 / SND_RATE;
			if (t < 0) {
				continue;
			}
			if (track->repeat) {
				if (track->loop_len <= 0) {
					continue;
				}
				t = fmod(t, track->loop_len);
			}
			for (n = 0; n < track->count; n++) {
				const SC_NOTE *note = &track->notes[n];
				double local = t - note->start;
				double phase;
				if (local < 0 || local >= note->len || !(note->freq > 0 && note->freq < SND_RATE)) {
					continue;
				}
				phase = local / 1000.0 * note->freq;
				phase -= floor(phase);
				v += ((phase < 0.5) ? 1.0 : -1.0) * note->vol * SND_GAIN;
			}
		}
		if (v > 1.0) v = 1.0;
		if (v < -1.0) v = -1.0;
		buf[i] = (short)(v * 32767.0);
	}
	snd_pos += SND_FRAMES;
	/* drop the tracks that have finished */
	prev = NULL;
	for (track = snd_tracks; track != NULL; track = next) {
		double t = (double)(snd_pos - track->start) * 1000.0 / SND_RATE;
		next = track->next;
		if (!track->repeat && t >= track->loop_len) {
			if (prev == NULL) {
				snd_tracks = next;
			} else {
				prev->next = next;
			}
			free_track(track);
			continue;
		}
		prev = track;
	}
	snd_unlock();
}

/*
 * sound_thread - keep the waveOut device fed
 */
static DWORD WINAPI sound_thread(LPVOID param)
{
	WAVEFORMATEX fmt;
	int i;

	ZeroMemory(&fmt, sizeof(fmt));
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = SND_RATE;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
	if (waveOutOpen(&snd_out, WAVE_MAPPER, &fmt, (DWORD_PTR)snd_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		/* no output device: sc_sound_play() stops queueing tracks nobody would consume */
		snd_out = NULL;
		snd_failed = TRUE;
		return 0;
	}
	ZeroMemory(snd_hdr, sizeof(snd_hdr));
	for (i = 0; i < SND_BUFFERS; i++) {
		snd_hdr[i].lpData = (LPSTR)snd_buf[i];
		snd_hdr[i].dwBufferLength = sizeof(snd_buf[i]);
		waveOutPrepareHeader(snd_out, &snd_hdr[i], sizeof(WAVEHDR));
		fill_buffer(snd_buf[i]);
		waveOutWrite(snd_out, &snd_hdr[i], sizeof(WAVEHDR));
	}
	while (!snd_quit) {
		WaitForSingleObject(snd_event, 100);
		if (snd_quit) {
			break;
		}
		for (i = 0; i < SND_BUFFERS; i++) {
			if (snd_hdr[i].dwFlags & WHDR_DONE) {
				fill_buffer(snd_buf[i]);
				waveOutWrite(snd_out, &snd_hdr[i], sizeof(WAVEHDR));
			}
		}
	}
	waveOutReset(snd_out);
	for (i = 0; i < SND_BUFFERS; i++) {
		waveOutUnprepareHeader(snd_out, &snd_hdr[i], sizeof(WAVEHDR));
	}
	waveOutClose(snd_out);
	snd_out = NULL;
	return 0;
}

/*
 * sound_start - start the mixer thread
 */
static BOOL sound_start(void)
{
	if (snd_thread != NULL) {
		return !snd_failed;
	}
	/* the critical section must exist before the mixer thread can race for it */
	if (!snd_cs_init) {
		InitializeCriticalSection(&snd_cs);
		snd_cs_init = TRUE;
	}
	snd_quit = 0;
	snd_pos = 0;
	snd_failed = FALSE;
	snd_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (snd_event == NULL) {
		return FALSE;
	}
	snd_thread = CreateThread(NULL, 0, sound_thread, NULL, 0, NULL);
	if (snd_thread == NULL) {
		CloseHandle(snd_event);
		snd_event = NULL;
		return FALSE;
	}
	SetThreadPriority(snd_thread, THREAD_PRIORITY_TIME_CRITICAL);
	return TRUE;
}

/*
 * sc_sound_play - queue a sequence of notes
 */
BOOL sc_sound_play(const SC_NOTE *notes, int count, BOOL repeat, int group)
{
	SND_TRACK *track;
	int i;

	if (notes == NULL || count <= 0) {
		return FALSE;
	}
	if (!sound_start()) {
		return FALSE;
	}
	track = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SND_TRACK));
	if (track == NULL) {
		return FALSE;
	}
	track->notes = HeapAlloc(GetProcessHeap(), 0, sizeof(SC_NOTE) * count);
	if (track->notes == NULL) {
		HeapFree(GetProcessHeap(), 0, track);
		return FALSE;
	}
	CopyMemory(track->notes, notes, sizeof(SC_NOTE) * count);
	track->count = count;
	track->repeat = repeat;
	track->group = group;
	track->loop_len = 0;
	for (i = 0; i < count; i++) {
		if (notes[i].start + notes[i].len > track->loop_len) {
			track->loop_len = notes[i].start + notes[i].len;
		}
	}
	if (repeat) {
		/* the web version restarts when the last note of the list ends */
		track->loop_len = notes[count - 1].start + notes[count - 1].len;
	}
	snd_lock();
	track->start = snd_pos;
	track->next = snd_tracks;
	snd_tracks = track;
	snd_unlock();
	return TRUE;
}

/*
 * sc_sound_stop - stop the tracks of a group (SOUND_GROUP_ALL: every track)
 */
void sc_sound_stop(int group)
{
	SND_TRACK *track, *prev = NULL, *next;

	if (snd_thread == NULL) {
		return;
	}
	snd_lock();
	for (track = snd_tracks; track != NULL; track = next) {
		next = track->next;
		if (group == SOUND_GROUP_ALL || track->group == group) {
			if (prev == NULL) {
				snd_tracks = next;
			} else {
				prev->next = next;
			}
			free_track(track);
			continue;
		}
		prev = track;
	}
	snd_unlock();
}

/*
 * sc_sound_shutdown - stop the mixer thread
 */
void sc_sound_shutdown(void)
{
	if (snd_thread == NULL) {
		return;
	}
	InterlockedExchange(&snd_quit, 1);
	SetEvent(snd_event);
	if (WaitForSingleObject(snd_thread, 3000) == WAIT_TIMEOUT) {
		TerminateThread(snd_thread, 0);
	}
	CloseHandle(snd_thread);
	snd_thread = NULL;
	CloseHandle(snd_event);
	snd_event = NULL;
	sc_sound_stop(SOUND_GROUP_ALL);
	snd_lock();
	while (snd_tracks != NULL) {
		SND_TRACK *next = snd_tracks->next;
		free_track(snd_tracks);
		snd_tracks = next;
	}
	snd_unlock();
}
/* End of source */
