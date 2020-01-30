
# Note: You may build ALmixer with these 3 options off for Android.
# The Android native OpenSL ES decoder can handle all these formats natively.
# However, we discovered that the Android native OpenSL ES decoder is REALLY slow.
# It also imposes a low number resource limit on the number of concurrent open streams (LoadStream).
# As such, for apps that push the use of audio, it is recommended using external libraries to decode these formats.
SET(ALMIXER_USE_OGG_VORBIS_DECODER "Use Ogg Vorbis decoder" ON)
SET(ALMIXER_USE_MPG123_DECODER "Use MPG123 decoder (LGPL)" ON)
# Note that the WAV decoder uses an implementation that compiles with the ALmixer source. So no external library needs to be linked for this.
SET(ALMIXER_USE_WAV_DECODER "Use WAV decoder (LGPL)" ON)



# OpenAL is required
SET(OPENAL_INCLUDE_DIR "${PREBUILT_LIBRARY_PATH}/openal-soft/include/AL" CACHE STRING "OpenAL include directory")
SET(OPENAL_LIBRARY "${PREBUILT_LIBRARY_PATH}/openal-soft/libs/${ANDROID_ABI}/libopenal.so" CACHE STRING "OpenAL library")


# The remaining are optional depending on how you set the options at the top.
SET(MPG123_INCLUDE_DIR "${PREBUILT_LIBRARY_PATH}/mpg123/include" CACHE STRING "mpg123 include directory")
SET(MPG123_LIBRARY "${PREBUILT_LIBRARY_PATH}/mpg123/libs/${ANDROID_ABI}/libmpg123.so" CACHE STRING "mpg123 library")

SET(OGG_INCLUDE_DIR "${PREBUILT_LIBRARY_PATH}/ogg/include" CACHE STRING "Ogg include directory")
SET(OGG_LIBRARY "${PREBUILT_LIBRARY_PATH}/ogg/libs/${ANDROID_ABI}/libogg.so" CACHE STRING "Ogg library")	
SET(VORBIS_INCLUDE_DIR "${PREBUILT_LIBRARY_PATH}/vorbis/include" CACHE STRING "vorbis include directory")
SET(VORBIS_LIBRARY "${PREBUILT_LIBRARY_PATH}/vorbis/libs/${ANDROID_ABI}/libvorbis.so" CACHE STRING "vorbis library")
SET(VORBIS_FILE_LIBRARY "${PREBUILT_LIBRARY_PATH}/vorbis/libs/${ANDROID_ABI}/libvorbisfile.so" CACHE STRING "vorbisfile library")


