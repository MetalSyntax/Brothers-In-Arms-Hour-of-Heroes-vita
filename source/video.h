/**
 * @file video.h
 * @brief Cutscene playback: MP4 demuxer + SceAvcdec H.264 + FFmpeg AAC (video_avc.c, from Zombie Infection).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Plays an H.264/AAC .mp4 to the end, blocking the caller (CROSS/CIRCLE/START skip).
 * @param path Full Vita path of the video.
 * @return 1 if played, 0 if missing or undecodable (the caller just continues: never a black hang).
 */
int video_play_avc(const char *path);

#ifdef __cplusplus
}
#endif
