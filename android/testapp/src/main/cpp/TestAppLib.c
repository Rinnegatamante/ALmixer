/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <string.h>
#include <jni.h>

#include <sys/types.h> // need for off_t for asset_manager because it looks like r9b broke something.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include "ALmixer.h"
#include "ALmixer_PlatformExtensions.h"
#include "al.h"
#define ALOGD(...) ((void)__android_log_print(ANDROID_LOG_INFO,  "HelloAndroidALmixer", __VA_ARGS__))
#define ALOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR,  "HelloAndroidALmixer", __VA_ARGS__))

/* This is a trivial JNI example where we use a native method
 * to return a new VM String. See the corresponding Java source
 * file located at:
 *
 *   apps/samples/hello-jni/project/src/com/example/hellojni/HelloJni.java
 */


static jobject s_proxyObject;
static JavaVM* s_javaVm;
static ALmixer_Data* s_backgroundMusicHandle = NULL;
static ALmixer_Data* s_speechHandle = NULL;
static ALmixer_Data* s_alertSoundHandle = NULL;
static ALmixer_Data* s_beepSoundHandle = NULL;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* java_vm, void* reserved)
{
	s_javaVm = java_vm;
	return JNI_VERSION_1_6;
}

static void MyNDKPlaybackFinishedCallback(ALint which_channel, ALuint al_source, ALmixer_Data* almixer_data, ALboolean finished_naturally, void* user_data)
{

	ALOGD("MyNDKPlaybackFinishedCallback on channel:%d source:%d finished_naturally:%d", which_channel, al_source, finished_naturally);

    JNIEnv* env;
	/* For demonstration purposes, call back into Java */

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
    int get_env_stat = (*s_javaVm)->GetEnv(s_javaVm, (void**)&env, JNI_VERSION_1_6);
    if(get_env_stat == JNI_EDETACHED)
	{
		jint attach_status = (*s_javaVm)->AttachCurrentThread(s_javaVm, &env, NULL);
		if(0 != attach_status)
		{
			ALOGE("AttachCurrentThread failed"); 
		}
    }
	else if(JNI_OK == get_env_stat)
	{
        // don't need to do anything
    }
	else if (get_env_stat == JNI_EVERSION)
	{
		ALOGE("GetEnv: version not supported"); 
    }

	/* Now that we've done the dance, we can actually call into Java now. */
    jclass clazz  = (*env)->GetObjectClass(env, s_proxyObject);
    jmethodID method_id = (*env)->GetMethodID(env, clazz, "MainActivity_MyJavaPlaybackFinishedCallbackTriggeredFromNDK", "(IIZ)V");
    (*env)->CallVoidMethod(env, s_proxyObject, method_id, which_channel, al_source, finished_naturally);


	/* Clean up: If we Attached the thread, we need to Detach it */
    if(get_env_stat == JNI_EDETACHED)
	{
		(*s_javaVm)->DetachCurrentThread(s_javaVm);
	}
}

/*
JNIEXPORT jboolean JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_doStaticActivityInit(JNIEnv* env, jclass activity_class, jobject activity_object)
{
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doStaticActivityInit");
	ALmixer_Android_Init(activity_class);
    return JNI_TRUE;
}
*/

JNIEXPORT jboolean JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_doInit(JNIEnv* env, jobject thiz, jobject java_asset_manager, jobject my_activity)
{
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doInit");
	
	/* AAssetManager is no longer used and instead, ALmixer_Android_Init() should be called with the activity/context. */
	/*
	AAssetManager* ndk_asset_manager = AAssetManager_fromJava(env, java_asset_manager);
	*/
	/* Saving the object instance so it can be used for calling back into Java. 
	 * I think I need to call NewGlobalRef on it because it must live past this function,
	 * otherwise I get the error: 
	 * JNI ERROR (app bug): accessed stale local reference
	 */
	s_proxyObject = (*env)->NewGlobalRef(env, thiz);

	/* It so happens that thiz is the Activity object which we need to initialize RWops */
//	ALmixer_Android_Init(env, my_activity);
	
	ALmixer_Android_SetApplicationContext(thiz);
	ALmixer_Init(0, 0, 0);

	/* debug info */
/*
	ALmixer_OutputOpenALInfo();
	ALmixer_OutputDecoders();
*/
    ALmixer_SetPlaybackFinishedCallback(MyNDKPlaybackFinishedCallback, NULL);

	ALmixer_Data* music_data = ALmixer_LoadStream("battle_hymn_of_the_republic.mp3", 0, 0, 0, 0, 0);
	if(NULL == music_data)
	{
		ALOGD("Failed to load: %s", ALmixer_GetError());
	}
	s_backgroundMusicHandle = music_data;

	s_speechHandle = ALmixer_LoadAll("TheDeclarationOfIndependencePreambleJFK.m4a", 0);
	if(NULL == s_speechHandle)
	{
		ALOGD("Failed to load: %s", ALmixer_GetError());
	}

	s_alertSoundHandle = ALmixer_LoadAll("AlertChordStroke.wav", 0);
	if(NULL == s_alertSoundHandle)
	{
		ALOGD("Failed to load: %s", ALmixer_GetError());
	}

	s_beepSoundHandle = ALmixer_LoadAll("BeepGMC500.ogg", 0);
	if(NULL == s_beepSoundHandle)
	{
		ALOGD("Failed to load: %s", ALmixer_GetError());
	}

	/* Reserve 2 channels (channel 0 and 1) to require manual control. 
	 * This makes it easy for me to know that I can always play my music on channel 0 and speech on channel 1.
	 * i.e. I know this channel is always free for music, and I know which channel to adjust the music volume if I choose to.
	 */
	ALmixer_ReserveChannels(2);

	/* Play the music on channel 0, loop forever */
	ALmixer_PlayChannel(0, music_data, -1);

	/* Play the speech on channel 1, don't loop */
	ALmixer_PlayChannel(1, s_speechHandle, 0);


    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_doPause(JNIEnv* env, jobject thiz)
{
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doPause");
	ALmixer_BeginInterruption();
}

JNIEXPORT void JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_doResume(JNIEnv* env, jobject thiz)
{
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doResume");
	ALmixer_EndInterruption();
}

JNIEXPORT void JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_doDestroy(JNIEnv* env, jobject thiz)
{
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doDestroy");
	ALmixer_HaltChannel(-1);

	ALmixer_FreeData(s_beepSoundHandle);
	s_beepSoundHandle = NULL;
	ALmixer_FreeData(s_alertSoundHandle);
	s_alertSoundHandle = NULL;

	ALmixer_FreeData(s_speechHandle);
	s_speechHandle = NULL;
	ALmixer_FreeData(s_backgroundMusicHandle);
	s_backgroundMusicHandle = NULL;

	ALmixer_Quit();
	
	/* Release the proxy object. */
	(*env)->DeleteGlobalRef(env, s_proxyObject);
	s_proxyObject = NULL;

	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_doDestroy end");
	
}

JNIEXPORT void JNICALL Java_net_playcontrol_almixer_testapp_MainActivity_playSound(JNIEnv* env, jobject thiz, jint sound_id)
{
//	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_playSound, sound_id:%d", sound_id);
	int which_channel;
	// For laziness, I just interpret integer ids to map to particular sounds.
	switch(sound_id)
	{
		case 1:
		{
			which_channel = ALmixer_PlayChannel(-1, s_alertSoundHandle, 0);
			break;
		}
		case 2:
		{
			which_channel = ALmixer_PlayChannel(-1, s_beepSoundHandle, 0);
			break;
		}
		default:
		{
			// Shouldn't hit this case, but the alert sound seems appropriate.
			which_channel = ALmixer_PlayChannel(-1, s_alertSoundHandle, 0);
			break;
		}
	}
/*
	if(which_channel < 0)
	{	
		ALOGD("Failed to play: %s", ALmixer_GetError());
	}
	ALOGD("Java_net_playcontrol_almixer_testapp_MainActivity_playSound ended, which_channel:%d", which_channel);
*/	
}


