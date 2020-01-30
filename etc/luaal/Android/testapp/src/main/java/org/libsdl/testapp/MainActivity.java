package net.playcontrol.almixer.testapp;

import android.content.Intent;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.content.res.AssetManager;

public class MainActivity extends AppCompatActivity
{

//	private final String TAG = "MainActivity";

	// Note: You only need to load mpg123, ogg, vorbis, vorbisfile if
	// you built ALmixer with each of those options enabled to use the external 3rd-party libraries
	// instead of relying completely on the built-in native OpenSL ES decoder which can also handle all these formats.
	// (It was discovered that Android's native OpenSL ES decoder is REALLY slow
	// and can impose a new low resource limit with the number of concurrent LoadStreams,
	// so shipping faster (but otherwise redundant) copies of the decoders is encouraged for apps that push the use of audio.)
	// Otherwise, you may omit them.
	// openal, ALmixer, TestAppLib must always be loaded.
	static {
		System.loadLibrary("SDL2");
		System.loadLibrary("SDL2main");
		System.loadLibrary("SDL_image");
		System.loadLibrary("TestAppLib");
	}

	/* A native method that is implemented by the
     * 'hello-jni' native library, which is packaged
     * with this application.
     */
//    public static native boolean doStaticActivityInit();
    public native boolean doInit(AssetManager java_asset_manager, MainActivity my_activity);
    public native void doPause();
    public native void doResume();
    public native void doDestroy();
    public native void playSound(int sound_id);


    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		/* This will pass the HelloAndroidALmixer activity class
		 * which ALmixer will capture to initialize the ALmixer_RWops system behind the scenes.
		 */
//        HelloAndroidALmixer.doStaticActivityInit();
		

		/* Once upon a time, we needed to pass the AssetManager to ALmixer.
		 * But that has been changed to conform to SDL's new behavior which
		 * requires the Activity class instead.
		 * The asset manager fetched here is no longer used (though Init still initializes ALmixer),
		 * but the code is left here as an example because this pattern is generally useful in Android.
		 */
		Log.i("MainActivity", "calling doInit");
		AssetManager java_asset_manager = this.getAssets();
		doInit(java_asset_manager, this);
		Log.i("MainActivity", "finished calling doInit");


	}

	/** Called when the activity is about to be paused. */
	@Override
	protected void onPause()
	{
		Log.i("MainActivity", "calling onPause");
		
		doPause();
		super.onPause();
	}

	@Override
	protected void onResume()
	{
		Log.i("MainActivity", "calling onResume");
		
		super.onResume();
		doResume();
	}

	/** Called when the activity is about to be destroyed. */
	@Override
	protected void onDestroy()
	{
		Log.i("MainActivity", "calling onDestroy");
		doDestroy();
		
		super.onDestroy();
		Log.i("MainActivity", "finished calling onDestroy");		
	}

/*
	@Override
	protected void onStart()
	{
		Log.i("MainActivity", "calling onStart");
		
		super.onStart();
				Log.i("MainActivity", "calling doInit");

		AssetManager java_asset_manager = this.getAssets();
		doInit(java_asset_manager);
		Log.i("MainActivity", "finished calling doInit");
	}

	@Override
	protected void onStop()
	{
		Log.i("MainActivity", "calling onStop");
		doDestroy();
		
		super.onStop();
		Log.i("MainActivity", "finished calling onStop");
		
	}
*/
    public void myClickHandler(View the_view)
	{
		switch(the_view.getId())
		{
        	case R.id.alert_button:
			{
				// For laziness, I just pass an int representing which sound I want to play which the NDK implementation will figure out.
				playSound(1);
				break;
			}
			case R.id.beep_button:
			{
				// For laziness, I just pass an int representing which sound I want to play which the NDK implementation will figure out.
				playSound(2);
				break;
			}
			default:
			{
				break;
			}
		}
	} 

	
	public void MainActivity_MyJavaPlaybackFinishedCallbackTriggeredFromNDK(int which_channel, int channel_source, boolean finished_naturally) 
	{
		Log.i("MainActivity", "MainActivity_MyJavaPlaybackFinishedCallbackTriggeredFromNDK: channel:" + which_channel + ", source:" + channel_source + ", finishedNaturally:" + finished_naturally);
	}


}


