
/*
 * Copyright (C) Eric Wing <ewing . public @ playcontrol.net>
 *
 */


#include <stddef.h> /* NULL */
#include <stdlib.h>
#include "vorbis/ivorbisfile.h"


#include "SoundDecoder.h"

#include "SoundDecoder_Internal.h"

typedef struct OggVorbisFileContainer
{
    OggVorbis_File* oggVorbisFile;	
} OggVorbisFileContainer;

static int OggVorbis_init(void);
static void OggVorbis_quit(void);
static int OggVorbis_open(Sound_Sample* sound_sample, const char* file_ext);
static void OggVorbis_close(Sound_Sample* sound_sample);
static size_t OggVorbis_read(Sound_Sample* sound_sample);
static int OggVorbis_rewind(Sound_Sample* sound_sample);
static int OggVorbis_seek(Sound_Sample* sound_sample, size_t ms);

static const char* extensions_oggvorbis[] = 
{
	"ogg",
	"oga",
	NULL 
};

const Sound_DecoderFunctions __Sound_DecoderFunctions_OGG =
{
    {
        extensions_oggvorbis,
        "Decode audio through Ogg Vorbis",
        "Eric Wing <ewing . public @ playcontrol.net>",
        "http://playcontrol.net"
    },
	
    OggVorbis_init,       /*   init() method */
    OggVorbis_quit,       /*   quit() method */
    OggVorbis_open,       /*   open() method */
    OggVorbis_close,      /*  close() method */
    OggVorbis_read,       /*   read() method */
    OggVorbis_rewind,     /* rewind() method */
    OggVorbis_seek        /*   seek() method */
};


/* Official definition:
typedef struct {
  size_t (*read_func)  (void *ptr, size_t size, size_t nmemb, void *datasource);
  int    (*seek_func)  (void *datasource, ogg_int64_t offset, int whence);
  int    (*close_func) (void *datasource);
  long   (*tell_func)  (void *datasource);
} ov_callbacks;
*/

static size_t OggVorbisReadCallback(void* ptr, size_t size, size_t nmemb, void* datasource)
{
	ALmixer_RWops* rw_ops = (ALmixer_RWops*)datasource;
	return ALmixer_RWread(rw_ops, ptr, size, nmemb);
}

static int OggVorbisSeekCallback(void *datasource, ogg_int64_t offset, int whence)
{
	ALmixer_RWops* rw_ops = (ALmixer_RWops*)datasource;
	return ALmixer_RWseek(rw_ops, offset, whence);
}

static int OggVorbisCloseCallback(void *datasource)
{
/*
	Close is handled elsewhere
*/
	return 0;
}

static long OggVorbisTellCallback(void *datasource)
{
	ALmixer_RWops* rw_ops = (ALmixer_RWops*)datasource;
	return ALmixer_RWtell(rw_ops);
}


static const ov_callbacks rwops_ov_callbacks =
{
	OggVorbisReadCallback,
	OggVorbisSeekCallback,
	OggVorbisCloseCallback,
	OggVorbisTellCallback
};


static int OggVorbis_init(void)
{
    return 1; 
}


static void OggVorbis_quit(void)
{
}


static int OggVorbis_open(Sound_Sample* sound_sample, const char* file_ext)
{
	int ret_val;
	vorbis_info* info_ov;
    OggVorbis_File* file_ov;	
	OggVorbisFileContainer* ogg_vorbis_file_container;

	Sound_SampleInternal* sample_internal = (Sound_SampleInternal*)sound_sample->opaque;
	
	sound_sample->flags = 0;

	ogg_vorbis_file_container = (OggVorbisFileContainer*)malloc(sizeof(OggVorbisFileContainer));
	BAIL_IF_MACRO(ogg_vorbis_file_container == NULL, ERR_OUT_OF_MEMORY, 0);

	file_ov = (OggVorbis_File*)malloc(sizeof(OggVorbis_File));
	BAIL_IF_MACRO(file_ov == NULL, ERR_OUT_OF_MEMORY, 0);

	
	ret_val = ov_open_callbacks(sample_internal->rw, file_ov, NULL, 0, rwops_ov_callbacks);
	BAIL_IF_MACRO(ret_val < 0, ERR_NOT_INITIALIZED, 0);

	ogg_vorbis_file_container->oggVorbisFile = file_ov;

	sample_internal->decoder_private = ogg_vorbis_file_container;
	   
	/* Get information about the OGG file */
	info_ov = ov_info(file_ov, -1);

	/* Start extracting info into our data structure. */
	sound_sample->actual.channels = (ALubyte)info_ov->channels;

  	if(ov_seekable(file_ov))
	{
		sound_sample->flags |= SOUND_SAMPLEFLAG_CANSEEK;
	}
	else
	{
		/* assume flags is already cleared */
	}

	/*
		get the total time in msec. 
		Note: it will fail for non-seekable, so maybe this should go in the seek block.
		But the docs don't say if that is the only condition it would fail.
	*/
	{
		/* Unlike Vorbis, Tremor returns time in milliseconds instead of seconds. */
		ogg_int64_t total_time_msec = ov_time_total(file_ov, -1);
		if(OV_EINVAL == total_time_msec)
		{
			sample_internal->total_time = -1;
		}
		else
		{
			sample_internal->total_time = (ptrdiff_t)(total_time_msec);
		}
	}

	sound_sample->actual.rate = (ALuint)info_ov->rate;



	/* TODO: SDL2 suppors float and 32-bit formats. Is that useful? */
	/* Vorbis has multiple "streams" which may be at different rates and all need to be decoded. */
	if(0 == sound_sample->desired.format)
	{
		sound_sample->actual.format = AUDIO_S16SYS;
	}
	else
	{	
		sound_sample->actual.format = sound_sample->desired.format;
	}

	SNDDBG(("OggVorbis: channels = %d\n", sound_sample->actual.channels));
	SNDDBG(("OggVorbis: sampling rate = %d\n",sound_sample->actual.rate));
	SNDDBG(("OggVorbis: total seconds of sample = %d\n", sample_internal->total_time));
	SNDDBG(("OggVorbis: sound_sample->actual.format = %d\n", sound_sample->actual.format));

	return 1;
}


static void OggVorbis_close(Sound_Sample* sound_sample)
{
	Sound_SampleInternal* sample_internal = (Sound_SampleInternal*) sound_sample->opaque;
	OggVorbisFileContainer* ogg_vorbis_file_container = (OggVorbisFileContainer *)sample_internal->decoder_private;
	
	ov_clear(ogg_vorbis_file_container->oggVorbisFile);
	free(ogg_vorbis_file_container->oggVorbisFile);
	free(ogg_vorbis_file_container);
}


static size_t OggVorbis_read(Sound_Sample* sound_sample)
{
	Sound_SampleInternal* internal_sample = (Sound_SampleInternal*)sound_sample->opaque;
	OggVorbisFileContainer* ogg_vorbis_file_container = (OggVorbisFileContainer *)internal_sample->decoder_private;
	size_t max_buffer_size = internal_sample->buffer_size;
	size_t free_bytes_remaining_to_fill = max_buffer_size;
	size_t total_bytes_read = 0;
	long actual_bytes_read = 0;

	int is_bigendian;
	int bytes_per_sample; /* "word" size parameter */
	int is_signed;
	int out_bitstream = 0;
	int hole_counter = 0;
	int loop_counter = 0;

    is_bigendian = ((sound_sample->actual.format & 0x1000) ? 1 : 0);
    bytes_per_sample = ((sound_sample->actual.format & 0xFF) / 8);
    is_signed = ((sound_sample->actual.format & 0x8000) ? 1 : 0);

	/*
		Official documentation:
		ov_read does not attempt to completely fill a large, passed in data buffer; it merely guarantees that the passed back data does not overflow the passed in buffer size. Large buffers may be filled by iteratively looping over calls to ov_read (incrementing the buffer pointer) until the original buffer is filled.

		So we need to keep reading until we fill the buffer.
	*/
	/* TODO: SDL2 suppors float and 32-bit formats and ov_read_float() is available. */
	while(free_bytes_remaining_to_fill > 0 && !(sound_sample->flags & SOUND_SAMPLEFLAG_EOF))
	{
		char* buffer_start_point = &(((char*)internal_sample->buffer)[total_bytes_read]);
		/* Tremor omits several parameters compared to Vorbis. */
		actual_bytes_read = ov_read(ogg_vorbis_file_container->oggVorbisFile, buffer_start_point, free_bytes_remaining_to_fill, &out_bitstream);
		if(OV_EBADLINK == actual_bytes_read)
		{
			SNDDBG(("Error: ov_read OV_EBADLINK\n"));
			SoundDecoder_SetError("Error: ov_read OV_EBADLINK");
			sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;
			return total_bytes_read;
		}
		else if(OV_EINVAL == actual_bytes_read)
		{
			SNDDBG(("Error: ov_read OV_EINVAL\n"));
			SoundDecoder_SetError("Error: ov_read OV_EINVAL");
			sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;

			return total_bytes_read;
		}
		else if(OV_HOLE == actual_bytes_read)
		{
			/* Not sure what to do. Can we recover from a hole? */
			hole_counter = hole_counter + 1;
			/* Let's keep going until we hit a max */
			if(hole_counter > 2)
			{
				/* give up */
				SNDDBG(("Error: ov_read OV_HOLE. Was unable to recover.\n"));
				SoundDecoder_SetError("Error: ov_read OV_HOLE. Was unable to recover.");
				sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;
			}
			else
			{
				loop_counter++;
				continue;
			}
			
		}
		/* shouldn't hit this case, but just in case ov_read introduces a new error value */
		else if(actual_bytes_read < 0)
		{
			SNDDBG(("Error: ov_read unexpected error\n"));
			SoundDecoder_SetError("Error: ov_read unexpected error");
			sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;
			
			return total_bytes_read;
		}
		else if(0 == actual_bytes_read)
		{
			/* 
				There is an edge case where we request byte amounts that are too small.
				This could happen after the first pass in the loop and the remainder to fill is too small.
				So to avoid incorrectly setting the EOF flag in the wrong case,
				we will not set EOF if not the first pass.
			*/
			if(loop_counter < 1)
			{
				sound_sample->flags |= SOUND_SAMPLEFLAG_EOF;
			}
			else
			{
				sound_sample->flags |= SOUND_SAMPLEFLAG_EAGAIN;
			}
			return total_bytes_read;
		}
		else
		{
			/* reset the hole counter */
			hole_counter = 0;
			/* reset the EAGAIN mask if it was set? */
			sound_sample->flags = sound_sample->flags & ~SOUND_SAMPLEFLAG_EAGAIN;

			total_bytes_read = total_bytes_read + actual_bytes_read;
			free_bytes_remaining_to_fill = free_bytes_remaining_to_fill - actual_bytes_read;
		}

		loop_counter++;
	}

	return total_bytes_read;
}

static int OggVorbis_seek(Sound_Sample* sound_sample, size_t ms)
{
	Sound_SampleInternal* internal_sample = (Sound_SampleInternal*)sound_sample->opaque;
	OggVorbisFileContainer* ogg_vorbis_file_container = (OggVorbisFileContainer *) internal_sample->decoder_private;
	/* Unlike Vorbis, Tremor uses time in milliseconds instead of seconds. */
	int ret_val = ov_time_seek(ogg_vorbis_file_container->oggVorbisFile, (ogg_int64_t)ms);
	if(OV_ENOSEEK == ret_val)
	{
		SNDDBG(("Error: ov_time_seek failed because bitstream is not seekable\n"));
		SoundDecoder_SetError("Error: ov_time_seek failed because bitstream is not seekable");
		sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;
		return 0;
	}
	else if(0 != ret_val)
	{
		SNDDBG(("Error: ov_time_seek failed\n"));
		SoundDecoder_SetError("Error: ov_time_seek failed");
		sound_sample->flags |= SOUND_SAMPLEFLAG_ERROR;
		return 0;
	}
	return 1;
}

static int OggVorbis_rewind(Sound_Sample* sound_sample)
{
	return OggVorbis_seek(sound_sample, 0);
}

