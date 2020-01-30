/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../ALmixer_internal.h"
#include "ALmixer_stdinc.h"

#if 0
#include "ALmixer_assert.h"
#endif /* #if 0 */

#if 0
#include "ALmixer_log.h"
#endif /* #if 0 */

#ifdef __ANDROID__

#if 0
#include "ALmixer_system.h"
#endif /* #if 0 */

#include "ALmixer_PlatformExtensions.h"
#include "ALmixer_android.h"

#if 0
#include <EGL/egl.h>

#include "../../events/ALmixer_events_c.h"
#include "../../video/android/ALmixer_androidkeyboard.h"
#include "../../video/android/ALmixer_androidtouch.h"
#include "../../video/android/ALmixer_androidvideo.h"
#include "../../video/android/ALmixer_androidwindow.h"
#include "../../joystick/android/ALmixer_sysjoystick_c.h"
#endif /* #if 0 */

#include <android/log.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <dlfcn.h>

/* Looks like Apportable has this in their al.h header which is triggering multiple define errors.
 * Why does everybody redo this same #define???
 */
#if 0
#define LOG_TAG "ALmixer_android"
/* #define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__) */
/* #define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__) */
#define LOGI(...) do {} while (false)
#define LOGE(...) do {} while (false)
#endif /* #if 0 */

/* Uncomment this to log messages entering and exiting methods in this file */
/* #define DEBUG_JNI */

static void ALmixer_Android_JNI_ThreadDestroyed(void*);

/*******************************************************************************
 This file links the Java side of Android with libsdl
*******************************************************************************/
#include <jni.h>
#include <android/log.h>
#include <stdbool.h>


/*******************************************************************************
                               Globals
*******************************************************************************/

static pthread_key_t mThreadKey;
static JavaVM* mJavaVM = NULL;

/* Main activity */
/*
static jclass mActivityClass;
*/
static jobject s_applicationContextObject = NULL;

/* 
	This sucks. Major hack. 
	There is no standardized way to get the Android Context (JNI isn't so hot either).
	Now that I support either SDL or IUP, this is problematic.
	SDL won't work unless it creates a main activity.
	IUP similar.
	Use dlsym to try IUP first (I don't want to create another dependency).
	TODO: Change API to pass context and maybe JNIEnv.
*/
typedef JNIEnv* (*iupAndroid_GetEnvThreadSafePROC)(void);
typedef jobject (*iupAndroid_GetApplicationPROC)(JNIEnv* jni_env);
typedef jobject (*iupAndroid_GetCurrentActivityPROC)(JNIEnv* jni_env);

typedef void* (*SDL_AndroidGetJNIEnvPROC)(void);
typedef void* (*SDL_AndroidGetActivityPROC)(void);

static jobject Internal_SetApplicationContext(jobject app_context)
{
	JNIEnv* jni_env = (JNIEnv*)ALmixer_Android_JNI_GetEnv();
	
	// Make sure to always clear the previous context because we are holding a strong reference which could intefere with the natural behavior of things if not careful.
	if(NULL != s_applicationContextObject)
	{
		(*jni_env)->DeleteGlobalRef(jni_env, s_applicationContextObject);
	}

	// This will happen when we Quit or just want to release the current Context
	if(NULL == app_context)
	{
		s_applicationContextObject = NULL;
		return NULL;
	}

	s_applicationContextObject = (*jni_env)->NewGlobalRef(jni_env, app_context);
	return s_applicationContextObject;
}


// Returns a local ref of the shared object.
static jobject Internal_GetApplicationContext()
{
	if(NULL != s_applicationContextObject)
	{
		// We also need to keep compatbility with SDL/BlurrrShim_AndroidGetActivity
		// The usual pattern is to call DeleteLocalRef which won't work if we return the GlobalRef shared pointer.
		// So make a new NewLocalRef. 
		//JNIEnv* jni_env = (JNIEnv*)ALmixer_Android_JNI_GetEnv();
		// return (*jni_env)->NewLocalRef(jni_env, s_applicationContextObject);

        // UPDATE: Drat: I think NewLocalRef will cause a leak because not everybody calls DeleteLocalRef, especially when using interchangably with SDL_GetActivity().
		// So I think I need to return the global object.
        return s_applicationContextObject;
	}


	JNIEnv* jni_env = NULL;
    jobject java_application_context = NULL;


    // Ugh! Android 6.0 seems to have changed/broken how dlsym works. 
    // Prior, I could just dlsym(RTLD_DEFAULT, "Function"); and get any function already loaded via System.loadLibrary.
    // Now I get nothing.
    // I seem to be able to do dlopen("libSDL2.so") without any path, presumably because System.loadLibrary already loaded it.
    // So I can get a handle to pass to dlsym which can then find the symbol.
    // I'm worried this is fragile too, but I don't have much choice.



    // First try to find IUP and get its Application Context.

	// GOTCHA: In IUP, we may get a local ref, and are expected to DeleteLocalRef after we are done.
	// But SDL just gives us a global ref and we are not supposed to free it.
	// So we need to handle the case differently. 
	// I don't want to force users to call DeleteLocalRef for this function, so we will
	// cache the value in a NewGlobalRef in both cases,
	// and call DeleteLocalRef for IUP.

	bool needs_to_delete_local_ref = false;
    void* iup_lib = NULL;
    // Old way to get function
    iupAndroid_GetEnvThreadSafePROC iupAndroid_GetEnvThreadSafeFnPtr = (iupAndroid_GetEnvThreadSafePROC)dlsym(RTLD_DEFAULT, "iupAndroid_GetEnvThreadSafe");
    iupAndroid_GetApplicationPROC iupAndroid_GetApplicationFnPtr = (iupAndroid_GetApplicationPROC)dlsym(RTLD_DEFAULT, "iupAndroid_GetApplication");
    if(NULL == iupAndroid_GetEnvThreadSafeFnPtr)
    {
        // fallback for Android 6.0+
        iup_lib = dlopen("libiup.so", RTLD_LOCAL|RTLD_LAZY);
        if(NULL != iup_lib)
        {
            iupAndroid_GetEnvThreadSafeFnPtr = (iupAndroid_GetEnvThreadSafePROC)dlsym(iup_lib, "iupAndroid_GetEnvThreadSafe");
            iupAndroid_GetApplicationFnPtr = (iupAndroid_GetApplicationPROC)dlsym(iup_lib, "iupAndroid_GetApplication");

        }
    }

	if((NULL != iupAndroid_GetEnvThreadSafeFnPtr) && (NULL != iupAndroid_GetApplicationFnPtr))
	{
		jni_env = iupAndroid_GetEnvThreadSafeFnPtr();
		if(NULL != jni_env)
		{
			java_application_context = iupAndroid_GetApplicationFnPtr(jni_env);
            if(NULL != java_application_context)
			{
				// We know we are using IUP and must call DeleteLocalRef.
				needs_to_delete_local_ref = true;
			}
		}
	}
    if(NULL != iup_lib)
    {
        dlclose(iup_lib);
        iup_lib = NULL;
    }


    void* sdl_lib = NULL;
    // Try to find SDL if IUP was not found.
	if((NULL == jni_env) || (NULL == java_application_context))
	{
        //__android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "Internal_GetApplicationContext SDL path");

        // Old way to get function
        SDL_AndroidGetActivityPROC SDL_AndroidGetActivityFnPtr = (SDL_AndroidGetActivityPROC)dlsym(RTLD_DEFAULT, "SDL_AndroidGetActivity");
        if(NULL == SDL_AndroidGetActivityFnPtr)
        {
            // fallback for Android 6.0+
            sdl_lib = dlopen("libSDL2.so", RTLD_LOCAL|RTLD_LAZY);
            if(NULL != sdl_lib)
            {
                SDL_AndroidGetActivityFnPtr = (SDL_AndroidGetActivityPROC)dlsym(sdl_lib, "SDL_AndroidGetActivity");
            }
		}
        
        // Now either the old or new way, if we get the function pointer, use it
        if(NULL != SDL_AndroidGetActivityFnPtr)
        {
            java_application_context = (jobject)SDL_AndroidGetActivityFnPtr();
            //__android_log_print(ANDROID_LOG_INFO, "ALmixer_android", "SDL_AndroidGetActivityFnPtr: %p", SDL_AndroidGetActivityFnPtr );
            //__android_log_print(ANDROID_LOG_INFO, "ALmixer_android", "java_application_context: %p", java_application_context );
        }

	}
    if(NULL != sdl_lib)
    {
        dlclose(sdl_lib);
        sdl_lib = NULL;
    }


	jobject return_java_application_context = NULL;

	if(NULL != java_application_context)
    {
		// save it for future calls. And return the global ref instead of the local ref so the user doesn't have to call DeleteLocalRef themselves.
		return_java_application_context = Internal_SetApplicationContext(java_application_context);
		// make sure to return the local ref version, not the global ref
        // __android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "Internal_GetApplicationContext end");
		if(needs_to_delete_local_ref)
		{
			(*jni_env)->DeleteLocalRef(jni_env, java_application_context);
			java_application_context = NULL;
		}
	}
	else
	{
		__android_log_print(ANDROID_LOG_WARN, "ALmixer_android", "Internal_GetApplicationContext Warning: No Application Context could be found. You must use ALmixer_Android_SetApplicationContext()");
	}

	// make sure to return the global ref, not local ref
	return return_java_application_context;
}

void ALmixer_Android_SetApplicationContext(void* application_context)
{
    Internal_SetApplicationContext(application_context);
}


void* ALmixer_Android_GetApplicationContext()
{
    return (void*)Internal_GetApplicationContext();
}

#if 0
/* method signatures */
static jmethodID midGetNativeSurface;
static jmethodID midFlipBuffers;
static jmethodID midAudioInit;
static jmethodID midAudioWriteShortBuffer;
static jmethodID midAudioWriteByteBuffer;
static jmethodID midAudioQuit;
static jmethodID midPollInputDevices;

/* Accelerometer data storage */
static float fLastAccelerometer[3];
static bool bHasNewData;
#endif /* #if 0 */

/*******************************************************************************
                 Functions called by JNI
*******************************************************************************/
#if 0
/* Library init */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    JNIEnv *env;
    mJavaVM = vm;
    LOGI("JNI_OnLoad called");
    if ((*mJavaVM)->GetEnv(mJavaVM, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
        LOGE("Failed to get the environment using GetEnv()");
        return -1;
    }
    /*
     * Create mThreadKey so we can keep track of the JNIEnv assigned to each thread
     * Refer to http://developer.android.com/guide/practices/design/jni.html for the rationale behind this
     */
    if (pthread_key_create(&mThreadKey, ALmixer_Android_JNI_ThreadDestroyed) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "SDL", "Error initializing pthread key");
    }
    ALmixer_Android_JNI_SetupThread();

    return JNI_VERSION_1_4;
}
#endif /* #if 0 */

/* Called before ALmixer_main() to initialize JNI bindings */
/* This symbol must be exported because SDLmain which resides in an external library must call this. */
#if 0
__attribute__ ((visibility("default"))) void ALmixer_Android_Init(JNIEnv* mEnv, jclass activity_class)
{
    __android_log_print(ANDROID_LOG_INFO, "SDL", "ALmixer_Android_Init()");

    ALmixer_Android_JNI_SetupThread();

    mActivityClass = (jclass)((*mEnv)->NewGlobalRef(mEnv, cls));

    midGetNativeSurface = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "getNativeSurface","()Landroid/view/Surface;");
    midFlipBuffers = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "flipBuffers","()V");
    midAudioInit = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "audioInit", "(IZZI)I");
    midAudioWriteShortBuffer = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "audioWriteShortBuffer", "([S)V");
    midAudioWriteByteBuffer = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "audioWriteByteBuffer", "([B)V");
    midAudioQuit = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "audioQuit", "()V");
    midPollInputDevices = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
                                "pollInputDevices", "()V");

    bHasNewData = false;

    if(!midGetNativeSurface || !midFlipBuffers || !midAudioInit ||
       !midAudioWriteShortBuffer || !midAudioWriteByteBuffer || !midAudioQuit || !midPollInputDevices) {
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL: Couldn't locate Java callbacks, check that they're named and typed correctly");
    }
    __android_log_print(ANDROID_LOG_INFO, "SDL", "ALmixer_Android_Init() finished!");
}
#endif /* #if 0 */


void ALmixer_Android_OnLoad(JavaVM* java_vm, void* reserved)
{
    mJavaVM = java_vm;
    /*
     * Create mThreadKey so we can keep track of the JNIEnv assigned to each thread
     * Refer to http://developer.android.com/guide/practices/design/jni.html for the rationale behind this
     */
    if (pthread_key_create(&mThreadKey, ALmixer_Android_JNI_ThreadDestroyed) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "Error initializing pthread key");
    }

    // I think this this can be deferred. With SDL, since it starts Android on a background thread, this would setup a JNIEnv for the main thread which will probably never be used. Deferring this until needed might be better.
//    ALmixer_Android_JNI_SetupThread();
    
//    return JNI_VERSION_1_6;
}

void* ALmixer_Android_GetJavaVM()
{
	return mJavaVM;
}

/* This was added for ALmixer to call into */
void ALmixer_Android_Core_Init()
{
    JNIEnv* env = NULL;
    /* Make sure the global variable for mJavaVM gets set */
    mJavaVM = ALmixer_Android_GetJavaVM();


	/*
    LOGI("ALmixer_Android_Core_Init called");
	*/
    /*
    if ((*mJavaVM)->GetEnv(mJavaVM, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "Failed to get the environment using GetEnv()");
        return;
    }
*/
    // This will do the GetEnv stuff
    ALmixer_Android_JNI_SetupThread();


    // call Internal_GetApplicationContext to trigger the hoop jumping to try to find it.
    Internal_GetApplicationContext();

    
}

/* This was added for ALmixer to call into */
void ALmixer_Android_Core_Quit()
{
    // We need to release the application context so the app can close.
    Internal_SetApplicationContext(NULL);

    // I don't know if I need to explicitly call DetachCurrentThread. I thought the pthread_key_create "destructor" is supposed to clean up.
    // In the SDL backend, I tried to call DetachCurrentThread because I saw a warning in the logcat, but it crashed the app.
    // It might be because I share the SDL one and I shouldn't touch it.
    // But in this case, I'm not sure what I need to do.
    // Commented out for now.
    /*
    (*mJavaVM)->DetachCurrentThread(mJavaVM);
    pthread_setspecific(mThreadKey, NULL);
    */



    // DO NOT set to null because we don't get OnLoad on subsequent launches
    // mJavaVM = NULL;
}


#if 0

/* Resize */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeResize(
                                    JNIEnv* env, jclass jcls,
                                    jint width, jint height, jint format)
{
    Android_SetScreenResolution(width, height, format);
}

// Paddown
JNIEXPORT int JNICALL Java_org_libsdl_app_SDLActivity_onNativePadDown(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint keycode)
{
    return Android_OnPadDown(device_id, keycode);
}

// Padup
JNIEXPORT int JNICALL Java_org_libsdl_app_SDLActivity_onNativePadUp(
                                   JNIEnv* env, jclass jcls,
                                   jint device_id, jint keycode)
{
    return Android_OnPadUp(device_id, keycode);
}

/* Joy */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeJoy(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint axis, jfloat value)
{
    Android_OnJoy(device_id, axis, value);
}

/* POV Hat */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeHat(
                                    JNIEnv* env, jclass jcls,
                                    jint device_id, jint hat_id, jint x, jint y)
{
    Android_OnHat(device_id, hat_id, x, y);
}


JNIEXPORT int JNICALL Java_org_libsdl_app_SDLActivity_nativeAddJoystick(
    JNIEnv* env, jclass jcls,
    jint device_id, jstring device_name, jint is_accelerometer, 
    jint nbuttons, jint naxes, jint nhats, jint nballs)
{
    int retval;
    const char *name = (*env)->GetStringUTFChars(env, device_name, NULL);

    retval = Android_AddJoystick(device_id, name, (ALmixer_bool) is_accelerometer, nbuttons, naxes, nhats, nballs);

    (*env)->ReleaseStringUTFChars(env, device_name, name);
    
    return retval;
}

JNIEXPORT int JNICALL Java_org_libsdl_app_SDLActivity_nativeRemoveJoystick(
    JNIEnv* env, jclass jcls, jint device_id)
{
    return Android_RemoveJoystick(device_id);
}


/* Surface Created */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(JNIEnv* env, jclass jcls)
{
    ALmixer_WindowData *data;
    ALmixer_VideoDevice *_this;

    if (Android_Window == NULL || Android_Window->driverdata == NULL ) {
        return;
    }
    
    _this =  ALmixer_GetVideoDevice();
    data =  (ALmixer_WindowData *) Android_Window->driverdata;
    
    /* If the surface has been previously destroyed by onNativeSurfaceDestroyed, recreate it here */
    if (data->egl_surface == EGL_NO_SURFACE) {
        if(data->native_window) {
            ANativeWindow_release(data->native_window);
        }
        data->native_window = ALmixer_Android_JNI_GetNativeWindow();
        data->egl_surface = ALmixer_EGL_CreateSurface(_this, (NativeWindowType) data->native_window);
    }
    
    /* GL Context handling is done in the event loop because this function is run from the Java thread */
    
}

/* Surface Destroyed */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(JNIEnv* env, jclass jcls)
{
    /* We have to clear the current context and destroy the egl surface here
     * Otherwise there's BAD_NATIVE_WINDOW errors coming from eglCreateWindowSurface on resume
     * Ref: http://stackoverflow.com/questions/8762589/eglcreatewindowsurface-on-ics-and-switching-from-2d-to-3d
     */
    ALmixer_WindowData *data;
    ALmixer_VideoDevice *_this;
    
    if (Android_Window == NULL || Android_Window->driverdata == NULL ) {
        return;
    }
    
    _this =  ALmixer_GetVideoDevice();
    data = (ALmixer_WindowData *) Android_Window->driverdata;
    
    if (data->egl_surface != EGL_NO_SURFACE) {
        ALmixer_EGL_MakeCurrent(_this, NULL, NULL);
        ALmixer_EGL_DestroySurface(_this, data->egl_surface);
        data->egl_surface = EGL_NO_SURFACE;
    }
    
    /* GL Context handling is done in the event loop because this function is run from the Java thread */

}

JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_nativeFlipBuffers(JNIEnv* env, jclass jcls)
{
    ALmixer_GL_SwapWindow(Android_Window);
}

/* Keydown */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeKeyDown(
                                    JNIEnv* env, jclass jcls, jint keycode)
{
    Android_OnKeyDown(keycode);
}

/* Keyup */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeKeyUp(
                                    JNIEnv* env, jclass jcls, jint keycode)
{
    Android_OnKeyUp(keycode);
}

/* Keyboard Focus Lost */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeKeyboardFocusLost(
                                    JNIEnv* env, jclass jcls)
{
    /* Calling ALmixer_StopTextInput will take care of hiding the keyboard and cleaning up the DummyText widget */
    ALmixer_StopTextInput();
}


/* Touch */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeTouch(
                                    JNIEnv* env, jclass jcls,
                                    jint touch_device_id_in, jint pointer_finger_id_in,
                                    jint action, jfloat x, jfloat y, jfloat p)
{
    Android_OnTouch(touch_device_id_in, pointer_finger_id_in, action, x, y, p);
}

/* Accelerometer */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_onNativeAccel(
                                    JNIEnv* env, jclass jcls,
                                    jfloat x, jfloat y, jfloat z)
{
    fLastAccelerometer[0] = x;
    fLastAccelerometer[1] = y;
    fLastAccelerometer[2] = z;
    bHasNewData = true;
}

/* Low memory */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_nativeLowMemory(
                                    JNIEnv* env, jclass cls)
{
    ALmixer_SendAppEvent(ALmixer_APP_LOWMEMORY);
}

/* Quit */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_nativeQuit(
                                    JNIEnv* env, jclass cls)
{
    /* Discard previous events. The user should have handled state storage
     * in ALmixer_APP_WILLENTERBACKGROUND. After nativeQuit() is called, no
     * events other than ALmixer_QUIT and ALmixer_APP_TERMINATING should fire */
    ALmixer_FlushEvents(ALmixer_FIRSTEVENT, ALmixer_LASTEVENT);
    /* Inject a ALmixer_QUIT event */
    ALmixer_SendQuit();
    ALmixer_SendAppEvent(ALmixer_APP_TERMINATING);
    /* Resume the event loop so that the app can catch ALmixer_QUIT which
     * should now be the top event in the event queue. */
    if (!ALmixer_SemValue(Android_ResumeSem)) ALmixer_SemPost(Android_ResumeSem);
}

/* Pause */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_nativePause(
                                    JNIEnv* env, jclass cls)
{
//    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "nativePause()");
    if (Android_Window) {
        ALmixer_SendWindowEvent(Android_Window, ALmixer_WINDOWEVENT_FOCUS_LOST, 0, 0);
        ALmixer_SendWindowEvent(Android_Window, ALmixer_WINDOWEVENT_MINIMIZED, 0, 0);
        ALmixer_SendAppEvent(ALmixer_APP_WILLENTERBACKGROUND);
        ALmixer_SendAppEvent(ALmixer_APP_DIDENTERBACKGROUND);
    
        /* *After* sending the relevant events, signal the pause semaphore 
         * so the event loop knows to pause and (optionally) block itself */
        if (!ALmixer_SemValue(Android_PauseSem)) ALmixer_SemPost(Android_PauseSem);
    }
}

/* Resume */
JNIEXPORT void JNICALL Java_org_libsdl_app_SDLActivity_nativeResume(
                                    JNIEnv* env, jclass cls)
{
//    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "nativeResume()");

    if (Android_Window) {
        ALmixer_SendAppEvent(ALmixer_APP_WILLENTERFOREGROUND);
        ALmixer_SendAppEvent(ALmixer_APP_DIDENTERFOREGROUND);
        ALmixer_SendWindowEvent(Android_Window, ALmixer_WINDOWEVENT_FOCUS_GAINED, 0, 0);
        ALmixer_SendWindowEvent(Android_Window, ALmixer_WINDOWEVENT_RESTORED, 0, 0);
        /* Signal the resume semaphore so the event loop knows to resume and restore the GL Context
         * We can't restore the GL Context here because it needs to be done on the SDL main thread
         * and this function will be called from the Java thread instead.
         */
        if (!ALmixer_SemValue(Android_ResumeSem)) ALmixer_SemPost(Android_ResumeSem);
    }
}

JNIEXPORT void JNICALL Java_org_libsdl_app_SDLInputConnection_nativeCommitText(
                                    JNIEnv* env, jclass cls,
                                    jstring text, jint newCursorPosition)
{
    const char *utftext = (*env)->GetStringUTFChars(env, text, NULL);

    ALmixer_SendKeyboardText(utftext);

    (*env)->ReleaseStringUTFChars(env, text, utftext);
}

JNIEXPORT void JNICALL Java_org_libsdl_app_SDLInputConnection_nativeSetComposingText(
                                    JNIEnv* env, jclass cls,
                                    jstring text, jint newCursorPosition)
{
    const char *utftext = (*env)->GetStringUTFChars(env, text, NULL);

    ALmixer_SendEditingText(utftext, 0, 0);

    (*env)->ReleaseStringUTFChars(env, text, utftext);
}

#endif /* #if 0 */


/*******************************************************************************
             Functions called by SDL into Java
*******************************************************************************/

static int s_active = 0;
struct LocalReferenceHolder
{
    JNIEnv *m_env;
    const char *m_func;
};

static struct LocalReferenceHolder LocalReferenceHolder_Setup(const char *func)
{
    struct LocalReferenceHolder refholder;
    refholder.m_env = NULL;
    refholder.m_func = func;
#ifdef DEBUG_JNI
    ALmixer_Log("Entering function %s", func);
#endif
    return refholder;
}

static ALmixer_bool LocalReferenceHolder_Init(struct LocalReferenceHolder *refholder, JNIEnv *env)
{
    const int capacity = 16;
    if ((*env)->PushLocalFrame(env, capacity) < 0) {
        ALmixer_SetError("Failed to allocate enough JVM local references");
        return ALMIXER_FALSE;
    }
    ++s_active;
    refholder->m_env = env;
    return ALMIXER_TRUE;
}

static void LocalReferenceHolder_Cleanup(struct LocalReferenceHolder *refholder)
{
#ifdef DEBUG_JNI
    ALmixer_Log("Leaving function %s", refholder->m_func);
#endif
    if (refholder->m_env) {
        JNIEnv* env = refholder->m_env;
        (*env)->PopLocalFrame(env, NULL);
        --s_active;
    }
}

static ALmixer_bool LocalReferenceHolder_IsActive()
{
    return s_active > 0;    
}

#if 0
ANativeWindow* ALmixer_Android_JNI_GetNativeWindow(void)
{
    ANativeWindow* anw;
    jobject s;
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();

    s = (*env)->CallStaticObjectMethod(env, mActivityClass, midGetNativeSurface);
    anw = ANativeWindow_fromSurface(env, s);
    (*env)->DeleteLocalRef(env, s);
  
    return anw;
}

void ALmixer_Android_JNI_SwapWindow()
{
    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();
    (*mEnv)->CallStaticVoidMethod(mEnv, mActivityClass, midFlipBuffers);
}

void ALmixer_Android_JNI_SetActivityTitle(const char *title)
{
    jmethodID mid;
    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();
    mid = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,"setActivityTitle","(Ljava/lang/String;)Z");
    if (mid) {
        jstring jtitle = (jstring)((*mEnv)->NewStringUTF(mEnv, title));
        (*mEnv)->CallStaticBooleanMethod(mEnv, mActivityClass, mid, jtitle);
        (*mEnv)->DeleteLocalRef(mEnv, jtitle);
    }
}

ALmixer_bool ALmixer_Android_JNI_GetAccelerometerValues(float values[3])
{
    int i;
    ALmixer_bool retval = ALMIXER_FALSE;

    if (bHasNewData) {
        for (i = 0; i < 3; ++i) {
            values[i] = fLastAccelerometer[i];
        }
        bHasNewData = false;
        retval = ALMIXER_TRUE;
    }

    return retval;
}
#endif /* #if 0 */

static void ALmixer_Android_JNI_ThreadDestroyed(void* value)
{
    //__android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "ALmixer_Android_JNI_ThreadDestroyed");
    
    /* The thread is being destroyed, detach it from the Java VM and set the mThreadKey value to NULL as required */
    JNIEnv *env = (JNIEnv*) value;
    if (env != NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "ALmixer_android", "DetachCurrentThread");
        (*mJavaVM)->DetachCurrentThread(mJavaVM);
        pthread_setspecific(mThreadKey, NULL);
    }
}

#if 1
JNIEnv* ALmixer_Android_JNI_GetEnv(void)
{
	JNIEnv* jni_env = NULL;
	   if(mJavaVM == NULL)
    {
			__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "ALmixer_Android_JNI_GetEnv mJavaVM is NULL"); 
    }


	/* ALmixer Notes: */
	/* Careful: If ALmixer is compiled with threads, make sure any calls back into Java are thread safe. */
	/* Unfortunately, JNI is making thread handling even more complicated than usual.
	 * If ALmixer is compiled with threads, it invokes callbacks on a ALmixer private background thread.
	 * In this case, we are required to call AttachCurrentThread for Java.
	 * However, there is a case in ALmixer where the callback doesn't happen on the background thread, but the calling thread.
	 * Calling ALmixer_HaltChannel() will trigger the callback on immediately on the thread you called the function on.
	 * (In this program, it is the main thread.)
	 * But JNI will break and crash if you try calling AttachCurrentThread in this case.
	 * So we need to know what thread we are on. If we are on the background thread, we must call AttachCurrentThread.
	 * Otherwise, we need to avoid calling it and use the current "env".
	 */

	/* There is a little JNI dance you can do to deal with this situation which is shown here.
	*/
	int get_env_stat = (*mJavaVM)->GetEnv(mJavaVM, (void**)&jni_env, JNI_VERSION_1_6);
	if(get_env_stat == JNI_EDETACHED)
	{
		jint attach_status = (*mJavaVM)->AttachCurrentThread(mJavaVM, &jni_env, NULL);
		if(0 != attach_status)
		{
			__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "AttachCurrentThread failed"); 
		}

		/* SDL notes: */
		/* From http://developer.android.com/guide/practices/jni.html
		 * Threads attached through JNI must call DetachCurrentThread before they exit. If coding this directly is awkward,
		 * in Android 2.0 (Eclair) and higher you can use pthread_key_create to define a destructor function that will be
		 * called before the thread exits, and call DetachCurrentThread from there. (Use that key with pthread_setspecific
		 * to store the JNIEnv in thread-local-storage; that way it'll be passed into your destructor as the argument.)
		 * Note: The destructor is not called unless the stored value is != NULL
		 * Note: You can call this function any number of times for the same thread, there's no harm in it
		 *       (except for some lost CPU cycles)
		 */
		pthread_setspecific(mThreadKey, (void*)jni_env);

	}
	else if(JNI_OK == get_env_stat)
	{
		// don't need to do anything
	}
	else if (get_env_stat == JNI_EVERSION)
	{
		__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "GetEnv version not supported"); 
	}

	return jni_env;

}
#else
// This was a comparison test implementation trying to debug a crash. Turns out the problem was a NULL JavaVM.
// Still, I wonder if there is an advantage to using the IUP/SDL JNIEnv functions if available.
// This would still need a fallback to use our own if not found.
JNIEnv* ALmixer_Android_JNI_GetEnv(void)
{
    
	JNIEnv* jni_env = NULL;

    if(mJavaVM == NULL)
    {
			__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "ALmixer_Android_JNI_GetEnv mJavaVM is NULL"); 
    }

    
//	int get_env_stat = (*mJavaVM)->GetEnv(mJavaVM, (void**)&jni_env, JNI_VERSION_1_6);
//			__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "ALmixer_Android_JNI_GetEnv mine jni_env=%p", jni_env); 
//    jni_env = NULL;
/*
	iupAndroid_GetEnvThreadSafePROC iupAndroid_GetEnvThreadSafeFnPtr = (iupAndroid_GetEnvThreadSafePROC)dlsym(RTLD_DEFAULT, "iupAndroid_GetEnvThreadSafe");

	if(NULL != iupAndroid_GetEnvThreadSafeFnPtr)
	{
		iupAndroid_GetApplicationPROC iupAndroid_GetApplicationFnPtr = (iupAndroid_GetApplicationPROC)dlsym(RTLD_DEFAULT, "iupAndroid_GetApplication");
		jni_env = iupAndroid_GetEnvThreadSafeFnPtr();
        return jni_env;
	}
*/
	if((NULL == jni_env))
	{
		SDL_AndroidGetJNIEnvPROC SDL_AndroidGetJNIEnvFNPtr = (SDL_AndroidGetJNIEnvPROC)dlsym(RTLD_DEFAULT, "SDL_AndroidGetJNIEnv");
		jni_env = (JNIEnv*)SDL_AndroidGetJNIEnvFNPtr();
			__android_log_print(ANDROID_LOG_ERROR, "ALmixer", "ALmixer_Android_JNI_GetEnv  SDL jni_env=%p", jni_env); 
        
        return jni_env;
	}
    
    return jni_env;

}
#endif

int ALmixer_Android_JNI_SetupThread(void)
{
    ALmixer_Android_JNI_GetEnv();
    return 1;
}

#if 0

/*
 * Audio support
 */
static jboolean audioBuffer16Bit = JNI_FALSE;
static jboolean audioBufferStereo = JNI_FALSE;
static jobject audioBuffer = NULL;
static void* audioBufferPinned = NULL;

int ALmixer_Android_JNI_OpenAudioDevice(int sampleRate, int is16Bit, int channelCount, int desiredBufferFrames)
{
    int audioBufferFrames;

    JNIEnv *env = ALmixer_Android_JNI_GetEnv();

    if (!env) {
        LOGE("callback_handler: failed to attach current thread");
    }
    ALmixer_Android_JNI_SetupThread();

    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "SDL audio: opening device");
    audioBuffer16Bit = is16Bit;
    audioBufferStereo = channelCount > 1;

    if ((*env)->CallStaticIntMethod(env, mActivityClass, midAudioInit, sampleRate, audioBuffer16Bit, audioBufferStereo, desiredBufferFrames) != 0) {
        /* Error during audio initialization */
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: error on AudioTrack initialization!");
        return 0;
    }

    /* Allocating the audio buffer from the Java side and passing it as the return value for audioInit no longer works on
     * Android >= 4.2 due to a "stale global reference" error. So now we allocate this buffer directly from this side. */

    if (is16Bit) {
        jshortArray audioBufferLocal = (*env)->NewShortArray(env, desiredBufferFrames * (audioBufferStereo ? 2 : 1));
        if (audioBufferLocal) {
            audioBuffer = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    }
    else {
        jbyteArray audioBufferLocal = (*env)->NewByteArray(env, desiredBufferFrames * (audioBufferStereo ? 2 : 1));
        if (audioBufferLocal) {
            audioBuffer = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    }

    if (audioBuffer == NULL) {
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: could not allocate an audio buffer!");
        return 0;
    }

    jboolean isCopy = JNI_FALSE;
    if (audioBuffer16Bit) {
        audioBufferPinned = (*env)->GetShortArrayElements(env, (jshortArray)audioBuffer, &isCopy);
        audioBufferFrames = (*env)->GetArrayLength(env, (jshortArray)audioBuffer);
    } else {
        audioBufferPinned = (*env)->GetByteArrayElements(env, (jbyteArray)audioBuffer, &isCopy);
        audioBufferFrames = (*env)->GetArrayLength(env, (jbyteArray)audioBuffer);
    }
    if (audioBufferStereo) {
        audioBufferFrames /= 2;
    }

    return audioBufferFrames;
}

void * ALmixer_Android_JNI_GetAudioBuffer()
{
    return audioBufferPinned;
}

void ALmixer_Android_JNI_WriteAudioBuffer()
{
    JNIEnv *mAudioEnv = ALmixer_Android_JNI_GetEnv();

    if (audioBuffer16Bit) {
        (*mAudioEnv)->ReleaseShortArrayElements(mAudioEnv, (jshortArray)audioBuffer, (jshort *)audioBufferPinned, JNI_COMMIT);
        (*mAudioEnv)->CallStaticVoidMethod(mAudioEnv, mActivityClass, midAudioWriteShortBuffer, (jshortArray)audioBuffer);
    } else {
        (*mAudioEnv)->ReleaseByteArrayElements(mAudioEnv, (jbyteArray)audioBuffer, (jbyte *)audioBufferPinned, JNI_COMMIT);
        (*mAudioEnv)->CallStaticVoidMethod(mAudioEnv, mActivityClass, midAudioWriteByteBuffer, (jbyteArray)audioBuffer);
    }

    /* JNI_COMMIT means the changes are committed to the VM but the buffer remains pinned */
}

void ALmixer_Android_JNI_CloseAudioDevice()
{
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();

    (*env)->CallStaticVoidMethod(env, mActivityClass, midAudioQuit);

    if (audioBuffer) {
        (*env)->DeleteGlobalRef(env, audioBuffer);
        audioBuffer = NULL;
        audioBufferPinned = NULL;
    }
}
#endif /* #if 0 */

/* Test for an exception and call ALmixer_SetError with its detail if one occurs */
/* If the parameter silent is truthy then ALmixer_SetError() will not be called. */
static bool ALmixer_Android_JNI_ExceptionOccurred(bool silent)
{
#if 0
    ALmixer_assert(LocalReferenceHolder_IsActive());
#endif /* #if 0 */
    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();

    jthrowable exception = (*mEnv)->ExceptionOccurred(mEnv);
    if (exception != NULL) {
        jmethodID mid;

        /* Until this happens most JNI operations have undefined behaviour */
        (*mEnv)->ExceptionClear(mEnv);

        if (!silent) {
            jclass exceptionClass = (*mEnv)->GetObjectClass(mEnv, exception);
            jclass classClass = (*mEnv)->FindClass(mEnv, "java/lang/Class");

            mid = (*mEnv)->GetMethodID(mEnv, classClass, "getName", "()Ljava/lang/String;");
            jstring exceptionName = (jstring)(*mEnv)->CallObjectMethod(mEnv, exceptionClass, mid);
            const char* exceptionNameUTF8 = (*mEnv)->GetStringUTFChars(mEnv, exceptionName, 0);

            mid = (*mEnv)->GetMethodID(mEnv, exceptionClass, "getMessage", "()Ljava/lang/String;");
            jstring exceptionMessage = (jstring)(*mEnv)->CallObjectMethod(mEnv, exception, mid);

            if (exceptionMessage != NULL) {
                const char* exceptionMessageUTF8 = (*mEnv)->GetStringUTFChars(mEnv, exceptionMessage, 0);
                ALmixer_SetError("%s: %s", exceptionNameUTF8, exceptionMessageUTF8);
                (*mEnv)->ReleaseStringUTFChars(mEnv, exceptionMessage, exceptionMessageUTF8);
            } else {
                ALmixer_SetError("%s", exceptionNameUTF8);
            }

            (*mEnv)->ReleaseStringUTFChars(mEnv, exceptionName, exceptionNameUTF8);
        }

        return true;
    }

    return false;
}

static int Internal_ALmixer_Android_JNI_FileOpen(ALmixer_RWops* ctx)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);

    int result = 0;

    jmethodID mid;
    jobject context;
    jobject assetManager;
    jobject inputStream;
    jclass channels;
    jobject readableByteChannel;
    jstring fileNameJString;
    jobject fd;
    jclass fdCls;
    jfieldID descriptor;

    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();
    if (!LocalReferenceHolder_Init(&refs, mEnv)) {
        goto failure;
    }

    fileNameJString = (jstring)ctx->hidden.androidio.fileNameRef;
    ctx->hidden.androidio.position = 0;

    /* ALmixer: The SDL code base assumes you are using a custom class called SDLActivity which implements its own getContext() function.
     * This is unfortunate because it won't work with any other activity.
     * I have the option of cutting directly to the AssetManager, or going to an intermediate step like the Context.
     * I've decided to go for the Activity object since it is a context and because it seems like it is potentially more flexible if I need access to more things.
     */
#if 0
    /* context = SDLActivity.getContext(); */
    mid = (*mEnv)->GetStaticMethodID(mEnv, mActivityClass,
            "getContext","()Landroid/content/Context;");
    context = (*mEnv)->CallStaticObjectMethod(mEnv, mActivityClass, mid);
#else
    context = Internal_GetApplicationContext();
#endif

    /* assetManager = context.getAssets(); */
    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, context),
            "getAssets", "()Landroid/content/res/AssetManager;");
    assetManager = (*mEnv)->CallObjectMethod(mEnv, context, mid);

    /* First let's try opening the file to obtain an AssetFileDescriptor.
    * This method reads the files directly from the APKs using standard *nix calls
    */
    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, assetManager), "openFd", "(Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;");
    inputStream = (*mEnv)->CallObjectMethod(mEnv, assetManager, mid, fileNameJString);
    if (ALmixer_Android_JNI_ExceptionOccurred(true)) {
        goto fallback;
    }

    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream), "getStartOffset", "()J");
    ctx->hidden.androidio.offset = (*mEnv)->CallLongMethod(mEnv, inputStream, mid);
    if (ALmixer_Android_JNI_ExceptionOccurred(true)) {
        goto fallback;
    }

    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream), "getDeclaredLength", "()J");
    ctx->hidden.androidio.size = (*mEnv)->CallLongMethod(mEnv, inputStream, mid);
    if (ALmixer_Android_JNI_ExceptionOccurred(true)) {
        goto fallback;
    }

    mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream), "getFileDescriptor", "()Ljava/io/FileDescriptor;");
    fd = (*mEnv)->CallObjectMethod(mEnv, inputStream, mid);
    fdCls = (*mEnv)->GetObjectClass(mEnv, fd);
    descriptor = (*mEnv)->GetFieldID(mEnv, fdCls, "descriptor", "I");
    ctx->hidden.androidio.fd = (*mEnv)->GetIntField(mEnv, fd, descriptor);
    ctx->hidden.androidio.assetFileDescriptorRef = (*mEnv)->NewGlobalRef(mEnv, inputStream);

    /* Seek to the correct offset in the file. */
    lseek(ctx->hidden.androidio.fd, (off_t)ctx->hidden.androidio.offset, SEEK_SET);

    if (false) {
fallback:
        /* Disabled log message because of spam on the Nexus 7 */
        /* __android_log_print(ANDROID_LOG_DEBUG, "SDL", "Falling back to legacy InputStream method for opening file"); */

        /* Try the old method using InputStream */
        ctx->hidden.androidio.assetFileDescriptorRef = NULL;

        /* inputStream = assetManager.open(<filename>); */
        mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, assetManager),
                "open", "(Ljava/lang/String;I)Ljava/io/InputStream;");
        inputStream = (*mEnv)->CallObjectMethod(mEnv, assetManager, mid, fileNameJString, 1 /* ACCESS_RANDOM */);
        if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
            goto failure;
        }

        ctx->hidden.androidio.inputStreamRef = (*mEnv)->NewGlobalRef(mEnv, inputStream);

        /* Despite all the visible documentation on [Asset]InputStream claiming
         * that the .available() method is not guaranteed to return the entire file
         * size, comments in <sdk>/samples/<ver>/ApiDemos/src/com/example/ ...
         * android/apis/content/ReadAsset.java imply that Android's
         * AssetInputStream.available() /will/ always return the total file size
        */
        
        /* size = inputStream.available(); */
        mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream),
                "available", "()I");
        ctx->hidden.androidio.size = (long)(*mEnv)->CallIntMethod(mEnv, inputStream, mid);
        if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
            goto failure;
        }

        /* readableByteChannel = Channels.newChannel(inputStream); */
        channels = (*mEnv)->FindClass(mEnv, "java/nio/channels/Channels");
        mid = (*mEnv)->GetStaticMethodID(mEnv, channels,
                "newChannel",
                "(Ljava/io/InputStream;)Ljava/nio/channels/ReadableByteChannel;");
        readableByteChannel = (*mEnv)->CallStaticObjectMethod(
                mEnv, channels, mid, inputStream);
        if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
            goto failure;
        }

        ctx->hidden.androidio.readableByteChannelRef =
            (*mEnv)->NewGlobalRef(mEnv, readableByteChannel);

        /* Store .read id for reading purposes */
        mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, readableByteChannel),
                "read", "(Ljava/nio/ByteBuffer;)I");
        ctx->hidden.androidio.readMethod = mid;
    }

    if (false) {
failure:
        result = -1;

        (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.fileNameRef);

        if(ctx->hidden.androidio.inputStreamRef != NULL) {
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.inputStreamRef);
        }

        if(ctx->hidden.androidio.readableByteChannelRef != NULL) {
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.readableByteChannelRef);
        }

        if(ctx->hidden.androidio.assetFileDescriptorRef != NULL) {
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.assetFileDescriptorRef);
        }

    }
    
    LocalReferenceHolder_Cleanup(&refs);
    return result;
}

int ALmixer_Android_JNI_FileOpen(ALmixer_RWops* ctx,
        const char* fileName, const char* mode)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();
    int retval;

    if (!LocalReferenceHolder_Init(&refs, mEnv)) {
        LocalReferenceHolder_Cleanup(&refs);        
        return -1;
    }

    if (!ctx) {
        LocalReferenceHolder_Cleanup(&refs);
        return -1;
    }

    jstring fileNameJString = (*mEnv)->NewStringUTF(mEnv, fileName);
    ctx->hidden.androidio.fileNameRef = (*mEnv)->NewGlobalRef(mEnv, fileNameJString);
    ctx->hidden.androidio.inputStreamRef = NULL;
    ctx->hidden.androidio.readableByteChannelRef = NULL;
    ctx->hidden.androidio.readMethod = NULL;
    ctx->hidden.androidio.assetFileDescriptorRef = NULL;

    retval = Internal_ALmixer_Android_JNI_FileOpen(ctx);
    LocalReferenceHolder_Cleanup(&refs);
    return retval;
}

size_t ALmixer_Android_JNI_FileRead(ALmixer_RWops* ctx, void* buffer,
        size_t size, size_t maxnum)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);

    if (ctx->hidden.androidio.assetFileDescriptorRef) {
        size_t bytesMax = size * maxnum;
        if (ctx->hidden.androidio.size != -1 /* UNKNOWN_LENGTH */ && ctx->hidden.androidio.position + bytesMax > ctx->hidden.androidio.size) {
            bytesMax = ctx->hidden.androidio.size - ctx->hidden.androidio.position;
        }
        size_t result = read(ctx->hidden.androidio.fd, buffer, bytesMax );
        if (result > 0) {
            ctx->hidden.androidio.position += result;
            LocalReferenceHolder_Cleanup(&refs);
            return result / size;
        }
        LocalReferenceHolder_Cleanup(&refs);
        return 0;
    } else {
        jlong bytesRemaining = (jlong) (size * maxnum);
        jlong bytesMax = (jlong) (ctx->hidden.androidio.size -  ctx->hidden.androidio.position);
        int bytesRead = 0;

        /* Don't read more bytes than those that remain in the file, otherwise we get an exception */
        if (bytesRemaining >  bytesMax) bytesRemaining = bytesMax;

        JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();
        if (!LocalReferenceHolder_Init(&refs, mEnv)) {
            LocalReferenceHolder_Cleanup(&refs);            
            return 0;
        }

        jobject readableByteChannel = (jobject)ctx->hidden.androidio.readableByteChannelRef;
        jmethodID readMethod = (jmethodID)ctx->hidden.androidio.readMethod;
        jobject byteBuffer = (*mEnv)->NewDirectByteBuffer(mEnv, buffer, bytesRemaining);

        while (bytesRemaining > 0) {
            /* result = readableByteChannel.read(...); */
            int result = (*mEnv)->CallIntMethod(mEnv, readableByteChannel, readMethod, byteBuffer);

            if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
                LocalReferenceHolder_Cleanup(&refs);            
                return 0;
            }

            if (result < 0) {
                break;
            }

            bytesRemaining -= result;
            bytesRead += result;
            ctx->hidden.androidio.position += result;
        }
        LocalReferenceHolder_Cleanup(&refs);                    
        return bytesRead / size;
    }
}

size_t ALmixer_Android_JNI_FileWrite(ALmixer_RWops* ctx, const void* buffer,
        size_t size, size_t num)
{
    ALmixer_SetError("Cannot write to Android package filesystem");
    return 0;
}

static int Internal_ALmixer_Android_JNI_FileClose(ALmixer_RWops* ctx, bool release)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);

    int result = 0;
    JNIEnv *mEnv = ALmixer_Android_JNI_GetEnv();

    if (!LocalReferenceHolder_Init(&refs, mEnv)) {
        LocalReferenceHolder_Cleanup(&refs);
        ALmixer_SetError("Failed to allocate enough JVM local references");
        return -1;
    }

    if (ctx) {
        if (release) {
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.fileNameRef);
        }

        if (ctx->hidden.androidio.assetFileDescriptorRef) {
            jobject inputStream = (jobject)ctx->hidden.androidio.assetFileDescriptorRef;
            jmethodID mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream),
                    "close", "()V");
            (*mEnv)->CallVoidMethod(mEnv, inputStream, mid);
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.assetFileDescriptorRef);
            if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
                result = -1;
            }
        }
        else {
            jobject inputStream = (jobject)ctx->hidden.androidio.inputStreamRef;

            /* inputStream.close(); */
            jmethodID mid = (*mEnv)->GetMethodID(mEnv, (*mEnv)->GetObjectClass(mEnv, inputStream),
                    "close", "()V");
            (*mEnv)->CallVoidMethod(mEnv, inputStream, mid);
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.inputStreamRef);
            (*mEnv)->DeleteGlobalRef(mEnv, (jobject)ctx->hidden.androidio.readableByteChannelRef);
            if (ALmixer_Android_JNI_ExceptionOccurred(false)) {
                result = -1;
            }
        }

        if (release) {
            ALmixer_FreeRW(ctx);
        }
    }

    LocalReferenceHolder_Cleanup(&refs);
    return result;
}


int64_t ALmixer_Android_JNI_FileSize(ALmixer_RWops* ctx)
{
    return ctx->hidden.androidio.size;
}

int64_t ALmixer_Android_JNI_FileSeek(ALmixer_RWops* ctx, int64_t offset, int whence)
{
    if (ctx->hidden.androidio.assetFileDescriptorRef) {
        switch (whence) {
            case RW_SEEK_SET:
                if (ctx->hidden.androidio.size != -1 /* UNKNOWN_LENGTH */ && offset > ctx->hidden.androidio.size) offset = ctx->hidden.androidio.size;
                offset += ctx->hidden.androidio.offset;
                break;
            case RW_SEEK_CUR:
                offset += ctx->hidden.androidio.position;
                if (ctx->hidden.androidio.size != -1 /* UNKNOWN_LENGTH */ && offset > ctx->hidden.androidio.size) offset = ctx->hidden.androidio.size;
                offset += ctx->hidden.androidio.offset;
                break;
            case RW_SEEK_END:
                offset = ctx->hidden.androidio.offset + ctx->hidden.androidio.size + offset;
                break;
            default:
                ALmixer_SetError("Unknown value for 'whence'");
                return -1;
        }
        whence = SEEK_SET;

        off_t ret = lseek(ctx->hidden.androidio.fd, (off_t)offset, SEEK_SET);
        if (ret == -1) return -1;
        ctx->hidden.androidio.position = ret - ctx->hidden.androidio.offset;
    } else {
        int64_t newPosition;

        switch (whence) {
            case RW_SEEK_SET:
                newPosition = offset;
                break;
            case RW_SEEK_CUR:
                newPosition = ctx->hidden.androidio.position + offset;
                break;
            case RW_SEEK_END:
                newPosition = ctx->hidden.androidio.size + offset;
                break;
            default:
                ALmixer_SetError("Unknown value for 'whence'");
                return -1;
        }

        /* Validate the new position */
        if (newPosition < 0) {
/* I didn't bring in SDL_Error. Cut to the chase and call ALmixer_SetError directly with the same string. */
#if 0
            return ALmixer_Error(ALMIXER_EFSEEK);
#else
			ALmixer_SetError("Error seeking in datastream");
            return -1;
#endif
        }
        if (newPosition > ctx->hidden.androidio.size) {
            newPosition = ctx->hidden.androidio.size;
        }

        int64_t movement = newPosition - ctx->hidden.androidio.position;
        if (movement > 0) {
            unsigned char buffer[4096];

            /* The easy case where we're seeking forwards */
            while (movement > 0) {
                int64_t amount = sizeof (buffer);
                if (amount > movement) {
                    amount = movement;
                }
                size_t result = ALmixer_Android_JNI_FileRead(ctx, buffer, 1, amount);
                if (result <= 0) {
                    /* Failed to read/skip the required amount, so fail */
                    return -1;
                }

                movement -= result;
            }

        } else if (movement < 0) {
            /* We can't seek backwards so we have to reopen the file and seek */
            /* forwards which obviously isn't very efficient */
            Internal_ALmixer_Android_JNI_FileClose(ctx, false);
            Internal_ALmixer_Android_JNI_FileOpen(ctx);
            ALmixer_Android_JNI_FileSeek(ctx, newPosition, RW_SEEK_SET);
        }
    }

    return ctx->hidden.androidio.position;

}

int ALmixer_Android_JNI_FileClose(ALmixer_RWops* ctx)
{
    return Internal_ALmixer_Android_JNI_FileClose(ctx, true);
}

#if 0
/* returns a new global reference which needs to be released later */
static jobject ALmixer_Android_JNI_GetSystemServiceObject(const char* name)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
    JNIEnv* env = ALmixer_Android_JNI_GetEnv();
    jobject retval = NULL;

    if (!LocalReferenceHolder_Init(&refs, env)) {
        LocalReferenceHolder_Cleanup(&refs);
        return NULL;
    }

    jstring service = (*env)->NewStringUTF(env, name);

    jmethodID mid;

    mid = (*env)->GetStaticMethodID(env, mActivityClass, "getContext", "()Landroid/content/Context;");
    jobject context = (*env)->CallStaticObjectMethod(env, mActivityClass, mid);

    mid = (*env)->GetMethodID(env, mActivityClass, "getSystemServiceFromUiThread", "(Ljava/lang/String;)Ljava/lang/Object;");
    jobject manager = (*env)->CallObjectMethod(env, context, mid, service);

    (*env)->DeleteLocalRef(env, service);

    retval = manager ? (*env)->NewGlobalRef(env, manager) : NULL;
    LocalReferenceHolder_Cleanup(&refs);
    return retval;
}

#define SETUP_CLIPBOARD(error) \
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__); \
    JNIEnv* env = ALmixer_Android_JNI_GetEnv(); \
    if (!LocalReferenceHolder_Init(&refs, env)) { \
        LocalReferenceHolder_Cleanup(&refs); \
        return error; \
    } \
    jobject clipboard = ALmixer_Android_JNI_GetSystemServiceObject("clipboard"); \
    if (!clipboard) { \
        LocalReferenceHolder_Cleanup(&refs); \
        return error; \
    }

#define CLEANUP_CLIPBOARD() \
    LocalReferenceHolder_Cleanup(&refs);

int ALmixer_Android_JNI_SetClipboardText(const char* text)
{
    SETUP_CLIPBOARD(-1)

    jmethodID mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, clipboard), "setText", "(Ljava/lang/CharSequence;)V");
    jstring string = (*env)->NewStringUTF(env, text);
    (*env)->CallVoidMethod(env, clipboard, mid, string);
    (*env)->DeleteGlobalRef(env, clipboard);
    (*env)->DeleteLocalRef(env, string);

    CLEANUP_CLIPBOARD();

    return 0;
}

char* ALmixer_Android_JNI_GetClipboardText()
{
    SETUP_CLIPBOARD(ALmixer_strdup(""))

    jmethodID mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, clipboard), "getText", "()Ljava/lang/CharSequence;");
    jobject sequence = (*env)->CallObjectMethod(env, clipboard, mid);
    (*env)->DeleteGlobalRef(env, clipboard);
    if (sequence) {
        mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, sequence), "toString", "()Ljava/lang/String;");
        jstring string = (jstring)((*env)->CallObjectMethod(env, sequence, mid));
        const char* utf = (*env)->GetStringUTFChars(env, string, 0);
        if (utf) {
            char* text = ALmixer_strdup(utf);
            (*env)->ReleaseStringUTFChars(env, string, utf);

            CLEANUP_CLIPBOARD();

            return text;
        }
    }

    CLEANUP_CLIPBOARD();    

    return ALmixer_strdup("");
}

ALmixer_bool ALmixer_Android_JNI_HasClipboardText()
{
    SETUP_CLIPBOARD(ALMIXER_FALSE)

    jmethodID mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, clipboard), "hasText", "()Z");
    jboolean has = (*env)->CallBooleanMethod(env, clipboard, mid);
    (*env)->DeleteGlobalRef(env, clipboard);

    CLEANUP_CLIPBOARD();
    
    return has ? ALMIXER_TRUE : ALMIXER_FALSE;
}


/* returns 0 on success or -1 on error (others undefined then)
 * returns truthy or falsy value in plugged, charged and battery
 * returns the value in seconds and percent or -1 if not available
 */
int ALmixer_Android_JNI_GetPowerInfo(int* plugged, int* charged, int* battery, int* seconds, int* percent)
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
    JNIEnv* env = ALmixer_Android_JNI_GetEnv();
    if (!LocalReferenceHolder_Init(&refs, env)) {
        LocalReferenceHolder_Cleanup(&refs);
        return -1;
    }

    jmethodID mid;

    mid = (*env)->GetStaticMethodID(env, mActivityClass, "getContext", "()Landroid/content/Context;");
    jobject context = (*env)->CallStaticObjectMethod(env, mActivityClass, mid);

    jstring action = (*env)->NewStringUTF(env, "android.intent.action.BATTERY_CHANGED");

    jclass cls = (*env)->FindClass(env, "android/content/IntentFilter");

    mid = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
    jobject filter = (*env)->NewObject(env, cls, mid, action);

    (*env)->DeleteLocalRef(env, action);

    mid = (*env)->GetMethodID(env, mActivityClass, "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
    jobject intent = (*env)->CallObjectMethod(env, context, mid, NULL, filter);

    (*env)->DeleteLocalRef(env, filter);

    cls = (*env)->GetObjectClass(env, intent);

    jstring iname;
    jmethodID imid = (*env)->GetMethodID(env, cls, "getIntExtra", "(Ljava/lang/String;I)I");

#define GET_INT_EXTRA(var, key) \
    iname = (*env)->NewStringUTF(env, key); \
    int var = (*env)->CallIntMethod(env, intent, imid, iname, -1); \
    (*env)->DeleteLocalRef(env, iname);

    jstring bname;
    jmethodID bmid = (*env)->GetMethodID(env, cls, "getBooleanExtra", "(Ljava/lang/String;Z)Z");

#define GET_BOOL_EXTRA(var, key) \
    bname = (*env)->NewStringUTF(env, key); \
    int var = (*env)->CallBooleanMethod(env, intent, bmid, bname, JNI_FALSE); \
    (*env)->DeleteLocalRef(env, bname);

    if (plugged) {
        GET_INT_EXTRA(plug, "plugged") /* == BatteryManager.EXTRA_PLUGGED (API 5) */
        if (plug == -1) {
            LocalReferenceHolder_Cleanup(&refs);
            return -1;
        }
        /* 1 == BatteryManager.BATTERY_PLUGGED_AC */
        /* 2 == BatteryManager.BATTERY_PLUGGED_USB */
        *plugged = (0 < plug) ? 1 : 0;
    }

    if (charged) {
        GET_INT_EXTRA(status, "status") /* == BatteryManager.EXTRA_STATUS (API 5) */
        if (status == -1) {
            LocalReferenceHolder_Cleanup(&refs);
            return -1;
        }
        /* 5 == BatteryManager.BATTERY_STATUS_FULL */
        *charged = (status == 5) ? 1 : 0;
    }

    if (battery) {
        GET_BOOL_EXTRA(present, "present") /* == BatteryManager.EXTRA_PRESENT (API 5) */
        *battery = present ? 1 : 0;
    }

    if (seconds) {
        *seconds = -1; /* not possible */
    }

    if (percent) {
        GET_INT_EXTRA(level, "level") /* == BatteryManager.EXTRA_LEVEL (API 5) */
        GET_INT_EXTRA(scale, "scale") /* == BatteryManager.EXTRA_SCALE (API 5) */
        if ((level == -1) || (scale == -1)) {
            LocalReferenceHolder_Cleanup(&refs);
            return -1;
        }
        *percent = level * 100 / scale;
    }

    (*env)->DeleteLocalRef(env, intent);

    LocalReferenceHolder_Cleanup(&refs);
    return 0;
}

/* returns number of found touch devices as return value and ids in parameter ids */
int ALmixer_Android_JNI_GetTouchDeviceIds(int **ids) {
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    jint sources = 4098; /* == InputDevice.SOURCE_TOUCHSCREEN */
    jmethodID mid = (*env)->GetStaticMethodID(env, mActivityClass, "inputGetInputDeviceIds", "(I)[I");
    jintArray array = (jintArray) (*env)->CallStaticObjectMethod(env, mActivityClass, mid, sources);
    int number = 0;
    *ids = NULL;
    if (array) {
        number = (int) (*env)->GetArrayLength(env, array);
        if (0 < number) {
            jint* elements = (*env)->GetIntArrayElements(env, array, NULL);
            if (elements) {
                int i;
                *ids = ALmixer_malloc(number * sizeof (**ids));
                for (i = 0; i < number; ++i) { /* not assuming sizeof (jint) == sizeof (int) */
                    (*ids)[i] = elements[i];
                }
                (*env)->ReleaseIntArrayElements(env, array, elements, JNI_ABORT);
            }
        }
        (*env)->DeleteLocalRef(env, array);
    }
    return number;
}

void ALmixer_Android_JNI_PollInputDevices()
{
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    (*env)->CallStaticVoidMethod(env, mActivityClass, midPollInputDevices);    
}

/* sends message to be handled on the UI event dispatch thread */
int ALmixer_Android_JNI_SendMessage(int command, int param)
{
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    if (!env) {
        return -1;
    }
    jmethodID mid = (*env)->GetStaticMethodID(env, mActivityClass, "sendMessage", "(II)Z");
    if (!mid) {
        return -1;
    }
    jboolean success = (*env)->CallStaticBooleanMethod(env, mActivityClass, mid, command, param);
    return success ? 0 : -1;
}

void ALmixer_Android_JNI_ShowTextInput(ALmixer_Rect *inputRect)
{
    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    if (!env) {
        return;
    }

    jmethodID mid = (*env)->GetStaticMethodID(env, mActivityClass, "showTextInput", "(IIII)Z");
    if (!mid) {
        return;
    }
    (*env)->CallStaticBooleanMethod(env, mActivityClass, mid,
                               inputRect->x,
                               inputRect->y,
                               inputRect->w,
                               inputRect->h );
}

void ALmixer_Android_JNI_HideTextInput()
{
    /* has to match Activity constant */
    const int COMMAND_TEXTEDIT_HIDE = 3;
    ALmixer_Android_JNI_SendMessage(COMMAND_TEXTEDIT_HIDE, 0);
}
#endif /* #if 0 */

/*
//////////////////////////////////////////////////////////////////////////////
//
// Functions exposed to SDL applications in ALmixer_system.h
//////////////////////////////////////////////////////////////////////////////
*/
#if 0
void *ALmixer_AndroidGetJNIEnv()
{
    return ALmixer_Android_JNI_GetEnv();
}
#endif /* #if 0 */


#if 0
void *ALmixer_AndroidGetActivity()
{
    /* See ALmixer_system.h for caveats on using this function. */

    jmethodID mid;

    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    if (!env) {
        return NULL;
    }

    /* return SDLActivity.getContext(); */
    mid = (*env)->GetStaticMethodID(env, mActivityClass,
            "getContext","()Landroid/content/Context;");
    return (*env)->CallStaticObjectMethod(env, mActivityClass, mid);
}
#endif /* #if 0 */

const char * ALmixer_AndroidGetInternalStoragePath()
{
    static char *s_AndroidInternalFilesPath = NULL;

    if (!s_AndroidInternalFilesPath) {
        struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
        jmethodID mid;
        jobject context;
        jobject fileObject;
        jstring pathString;
        const char *path;

        JNIEnv *env = ALmixer_Android_JNI_GetEnv();
        if (!LocalReferenceHolder_Init(&refs, env)) {
            LocalReferenceHolder_Cleanup(&refs);
            return NULL;
        }

    /* The SDL code base assumes you are using a custom class called SDLActivity which implements its own getContext() function.
     * This is unfortunate because it won't work with any other activity.
     * I have the option of cutting directly to the AssetManager, or going to an intermediate step like the Context.
     * I've decided to go for the Activity object since it is a context and because it seems like it is potentially more flexible if I need access to more things.
     */
#if 0
        /* context = SDLActivity.getContext(); */
        mid = (*env)->GetStaticMethodID(env, mActivityClass,
                "getContext","()Landroid/content/Context;");
        context = (*env)->CallStaticObjectMethod(env, mActivityClass, mid);
#else
        context = Internal_GetApplicationContext();
#endif
        /* fileObj = context.getFilesDir(); */
        mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, context),
                "getFilesDir", "()Ljava/io/File;");
        fileObject = (*env)->CallObjectMethod(env, context, mid);
        if (!fileObject) {
            ALmixer_SetError("Couldn't get internal directory");
            LocalReferenceHolder_Cleanup(&refs);
            return NULL;
        }

        /* path = fileObject.getAbsolutePath(); */
        mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, fileObject),
                "getAbsolutePath", "()Ljava/lang/String;");
        pathString = (jstring)(*env)->CallObjectMethod(env, fileObject, mid);

        path = (*env)->GetStringUTFChars(env, pathString, NULL);
        s_AndroidInternalFilesPath = ALmixer_strdup(path);
        (*env)->ReleaseStringUTFChars(env, pathString, path);

        LocalReferenceHolder_Cleanup(&refs);
    }
    return s_AndroidInternalFilesPath;
}

#if 0
int ALmixer_AndroidGetExternalStorageState()
{
    struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
    jmethodID mid;
    jclass cls;
    jstring stateString;
    const char *state;
    int stateFlags;

    JNIEnv *env = ALmixer_Android_JNI_GetEnv();
    if (!LocalReferenceHolder_Init(&refs, env)) {
        LocalReferenceHolder_Cleanup(&refs);
        return 0;
    }

    cls = (*env)->FindClass(env, "android/os/Environment");
    mid = (*env)->GetStaticMethodID(env, cls,
            "getExternalStorageState", "()Ljava/lang/String;");
    stateString = (jstring)(*env)->CallStaticObjectMethod(env, cls, mid);

    state = (*env)->GetStringUTFChars(env, stateString, NULL);

    /* Print an info message so people debugging know the storage state */
    __android_log_print(ANDROID_LOG_INFO, "SDL", "external storage state: %s", state);

    if (ALmixer_strcmp(state, "mounted") == 0) {
        stateFlags = ALmixer_ANDROID_EXTERNAL_STORAGE_READ |
                     ALmixer_ANDROID_EXTERNAL_STORAGE_WRITE;
    } else if (ALmixer_strcmp(state, "mounted_ro") == 0) {
        stateFlags = ALmixer_ANDROID_EXTERNAL_STORAGE_READ;
    } else {
        stateFlags = 0;
    }
    (*env)->ReleaseStringUTFChars(env, stateString, state);

    LocalReferenceHolder_Cleanup(&refs);
    return stateFlags;
}

const char * ALmixer_AndroidGetExternalStoragePath()
{
    static char *s_AndroidExternalFilesPath = NULL;

    if (!s_AndroidExternalFilesPath) {
        struct LocalReferenceHolder refs = LocalReferenceHolder_Setup(__FUNCTION__);
        jmethodID mid;
        jobject context;
        jobject fileObject;
        jstring pathString;
        const char *path;

        JNIEnv *env = ALmixer_Android_JNI_GetEnv();
        if (!LocalReferenceHolder_Init(&refs, env)) {
            LocalReferenceHolder_Cleanup(&refs);
            return NULL;
        }

        /* context = SDLActivity.getContext(); */
        mid = (*env)->GetStaticMethodID(env, mActivityClass,
                "getContext","()Landroid/content/Context;");
        context = (*env)->CallStaticObjectMethod(env, mActivityClass, mid);

        /* fileObj = context.getExternalFilesDir(); */
        mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, context),
                "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");
        fileObject = (*env)->CallObjectMethod(env, context, mid, NULL);
        if (!fileObject) {
            ALmixer_SetError("Couldn't get external directory");
            LocalReferenceHolder_Cleanup(&refs);
            return NULL;
        }

        /* path = fileObject.getAbsolutePath(); */
        mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, fileObject),
                "getAbsolutePath", "()Ljava/lang/String;");
        pathString = (jstring)(*env)->CallObjectMethod(env, fileObject, mid);

        path = (*env)->GetStringUTFChars(env, pathString, NULL);
        s_AndroidExternalFilesPath = ALmixer_strdup(path);
        (*env)->ReleaseStringUTFChars(env, pathString, path);

        LocalReferenceHolder_Cleanup(&refs);
    }
    return s_AndroidExternalFilesPath;
}
#endif /* #if 0 */

#endif /* __ANDROID__ */

/* vi: set ts=4 sw=4 expandtab: */

