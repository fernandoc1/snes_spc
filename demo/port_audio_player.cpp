#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <portaudio.h>

#include "snes_spc/spc.h"
#include "demo_util.h"

SNES_SPC* snes_spc = NULL;
SPC_Filter* filter = NULL;

// Callback function for audio playback
static int audioCallback(const void *inputBuffer, void *outputBuffer,
                         unsigned long framesPerBuffer,
                         const PaStreamCallbackTimeInfo *timeInfo,
                         PaStreamCallbackFlags statusFlags,
                         void *userData) {
    float *out = (float *)outputBuffer;
    (void)inputBuffer; // Prevent unused variable warning

	error(spc_play(snes_spc, framesPerBuffer, (short*)outputBuffer));

	/* Filter samples */
	spc_filter_run(filter, (spc_sample_t*)outputBuffer, framesPerBuffer);

    return paContinue;
}

int main(int argc, char** argv) {
    /* Create emulator and filter */
	snes_spc = spc_new();
	filter = spc_filter_new();
	if ( !snes_spc || !filter ) error( "Out of memory" );

	/* Load SPC */
	{
		/* Load file into memory */
		long spc_size;
		void* spc = load_file( (argc > 1) ? argv [1] : "test.spc", &spc_size );

		/* Load SPC data into emulator */
		error( spc_load_spc( snes_spc, spc, spc_size ) );
		free( spc ); /* emulator makes copy of data */

		/* Most SPC files have garbage data in the echo buffer, so clear that */
		spc_clear_echo( snes_spc );

		/* Clear filter before playing */
		spc_filter_clear( filter );
	}

    PaError err;
    PaStream *stream;

    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    int frames_per_buffer = 2048;
    err = Pa_OpenDefaultStream(&stream, 0, 1, paInt16, spc_sample_rate * 2, frames_per_buffer, audioCallback, NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    printf("Playing audio... Press Enter to stop.\n");
    getchar();

    err = Pa_StopStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    Pa_Terminate();

    return 0;
}