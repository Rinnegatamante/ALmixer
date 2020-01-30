#ifdef __ANDROID__
#include "ALmixer.h"
#include "ALmixer_PlatformExtensions.h"
#include <jni.h>
#include <stddef.h>

#ifndef ALMIXER_COMPILE_WITH_SDL
	#include "ALmixer_android.h"
#else
	#include "SDL.h"
#endif




#ifndef ALMIXER_COMPILE_WITH_SDL
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* java_vm, void* reserved)
{
	ALmixer_Android_OnLoad(java_vm, reserved);
    return JNI_VERSION_1_6;
}

// defined in ALmixer_android.c: void* ALmixer_Android_GetJavaVM()


#else
static JavaVM* s_javaVM = NULL;
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* java_vm, void* reserved)
{
    s_javaVM = java_vm;

    return JNI_VERSION_1_6;
}
void* ALmixer_Android_GetJavaVM()
{
	return s_javaVM;
}

#endif







void ALmixer_Android_Init()
{
#ifndef ALMIXER_COMPILE_WITH_SDL
	ALmixer_Android_Core_Init();
#endif

}

/* Do I need a Quit function to release the Activity class? */
void ALmixer_Android_Quit()
{
#ifndef ALMIXER_COMPILE_WITH_SDL
	ALmixer_Android_Core_Quit();
#endif

}

void* ALmixer_Android_GetJNIEnv(void)
{
#ifndef ALMIXER_COMPILE_WITH_SDL
	return ALmixer_Android_JNI_GetEnv();
#else
	return SDL_AndroidGetJNIEnv();
#endif
}

// When not compiled with SDL, the definition is in StandAlone/SDL/src/core/android/ALmixer_android.c
// But when using SDL, we must provide an implementation.
#ifndef ALMIXER_COMPILE_WITH_SDL
#else
void* ALmixer_Android_GetApplicationContext()
{
	return SDL_AndroidGetActivity();
}

void ALmixer_Android_SetApplicationContext(void* application_context)
{
	// no-op, SDL doesn't allow setting.
}

#endif





#endif /* #ifdef __ANDROID__ */


