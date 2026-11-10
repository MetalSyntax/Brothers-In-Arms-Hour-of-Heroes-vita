/* hosttest: sin SceAvcdec en Linux. Prueba el camino "video ausente/no decodificable":
 * hook_PlayMovie (patch.c) debe seguir sin colgarse y el juego avanzar. */
#include "log.h"
#include "video.h"

int video_play_avc(const char *path)
{
    l_warn("[VIDEO] hosttest: no SceAvcdec, %s skipped", path);
    return 0;
}
