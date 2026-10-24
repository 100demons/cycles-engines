/* ---- : the shared, silent placeholder machine (ARCHITECTURE.md §4, option A of the Cycles Mod Standard).

   A build holds slots 7 … the highest ticked engine; every unticked slot below that is filled with this silent
   `----` machine, so a saved sound finds its engine whatever else the build holds. The composer places this code
   ONCE however many gaps, and points every gap's update/render at it.

   Contract of the voice loop (One-Shot TECHNICAL §5): ph_update(pmod, v, p) then ph_render(out, v), once per block
   of 32 frames. update does nothing (rts); render writes 32 zero frames — silence, whatever the amp chain does.
   No patch points, no variables: the object is 22 bytes. */
#include <stdint.h>

typedef int32_t s32;
typedef uint32_t u32;

void ph_update(s32 pmod, char *v, const char *p)
{
	(void)pmod; (void)v; (void)p;
}

void ph_render(s32 *out, char *v)
{
	u32 i;
	(void)v;
	for (i = 0; i < 32; i++)
		out[i] = 0;
}
