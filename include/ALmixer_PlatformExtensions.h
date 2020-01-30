/*
 * Platform specific public APIs are intended to go here.
 */

#ifndef _ALMIXER_PLATFORMEXTENSIONS_H_
#define _ALMIXER_PLATFORMEXTENSIONS_H_

#ifdef __ANDROID__

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

#include "ALmixer.h"
/**
 * On Android when ALmixer is compiled without SDL, you MUST provide an application context class.
 * Call this function right after ALmixer_Init.
 * The Android AssetManager requires a context (which your main Activity provides or Application Context), which 
 * as a standalone libary, ALmixer has no knowledge of. So in order for any file related 
 * access to work, you MUST provide the Activity class on initialization so ALmixer can
 * access your files.
 * When compiled with SDL as the backend, this function is still safe to call and is currently a no-op.
 *
 * @note When compiled without SDL, if you are using SDL or IUP, hoops are jumped through to automatically find a valid context for you so you can avoid this function.
 * @note API uses void* to avoid needing to include <jni.h> because it brings in a lot of stuff.
 *
 * @param application_context A JNI jobject of a Context instance. Activities are valid contexts. Make sure your context intends to live for the life of ALmixer. ALmixer holds a strong reference until ALmixer_Quit or the context is changed.
 *
 */
extern ALMIXER_DECLSPEC void ALMIXER_CALL ALmixer_Android_SetApplicationContext(void* application_context);

/**
 * Get the application context.
 * This will return the application context being used by ALmixer.
 * @note API uses void* to avoid needing to include <jni.h> because it brings in a lot of stuff.
 * @return JNIEnv* from JNI. When compiled with SDL, this calls SDL_AndroidGetActivity().
 */
extern ALMIXER_DECLSPEC void* ALMIXER_CALL ALmixer_Android_GetApplicationContext(void);



/**
 * You probably won't need this, but just in case, it is provided.
 * @note API uses void* to avoid needing to include <jni.h> because it brings in a lot of stuff.
 * @return JavaVM* from JNI.
 */
extern ALMIXER_DECLSPEC void* ALMIXER_CALL ALmixer_Android_GetJavaVM(void);


/**
 * You probably won't need this, but just in case, it is provided.
 * @note API uses void* to avoid needing to include <jni.h> because it brings in a lot of stuff.
 * @return JNIEnv* from JNI. When compiled with SDL, this calls SDL_AndroidGetJNIEnv().
 */
extern ALMIXER_DECLSPEC void* ALMIXER_CALL ALmixer_Android_GetJNIEnv(void);




/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* #ifdef __ANDROID__ */


#endif /* _ALMIXER_PLATFORMEXTENSIONS_H_ */


