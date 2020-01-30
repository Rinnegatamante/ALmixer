#include <stddef.h>
#include "SoundDecoder.h"
#include "SoundDecoder_Internal.h"

static const char* extensions_Dummy[] =
{
	NULL
};

static int SoundDecoderDummy_init(void)
{
	return 0;
}

// This is compiled directly into ALmixer but is a placeholder which will be replaced at runtime with the real implementation when WMF is compiled as a plugin.
const Sound_DecoderFunctions __Sound_DecoderFunctions_WindowsMediaFoundationProxy =
{
	{
		extensions_Dummy,
		"WindowsMediaFoundationProxy", // The placeholder code looks for this exact string to know which list entry to replacce.
		"Eric Wing <ewing . public @ playcontrol.net>",
		"http://playcontrol.net"
	},

	SoundDecoderDummy_init,       /*   init() method */
	NULL,       /*   quit() method */
	NULL,       /*   open() method */
	NULL,      /*  close() method */
	NULL,       /*   read() method */
	NULL,     /* rewind() method */
	NULL        /*   seek() method */

};
