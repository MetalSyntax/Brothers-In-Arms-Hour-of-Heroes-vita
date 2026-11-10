/**
 * @file coreaudio.h
 * @brief BGM streamer driven by the SoundEngine_*BackgroundMusic* hooks (patch.c).
 */
#pragma once

int bgm_load(const char *path);  /**< Opens <path> with .m4a swapped for the converted .wav. 0 = ok. */
int bgm_start(int loop);         /**< Starts the loaded track (loop != 0 repeats it). */
int bgm_stop(int at_end);        /**< at_end != 0: stop after the current pass; else stop now. */
int bgm_unload(void);            /**< Stops and closes the track. */
void bgm_pause(int paused);      /**< Pause (1) / resume (0) without losing the position. */
int bgm_is_playing(void);        /**< 1 while a track is audible. */
void bgm_set_volume(float v);    /**< 0..1 software gain. */
