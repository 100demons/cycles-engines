/*
 * Shared fixed-point arithmetic for the Rhythmicon V5 Cloud engine.
 *
 * The EMAC helpers are copied from HALO (and Feral): each product is truncated to 2^-39, the products are summed,
 * then shifted by 31 and saturated to 32 bits. The same text is compiled for the Cycles and for the computer, so the
 * host proof compares the same arithmetic.
 */
#ifndef R5_COMMON_H
#define R5_COMMON_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;

#ifdef __mcoldfire__
static inline s32 qmul(s32 a, s32 b)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(b));
	return r;
}
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d));
	return r;
}
static inline s32 qmac4(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h)
{
	s32 r;
	__asm__ volatile("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmac.l %7,%8,%%acc0\n\t"
			 "movclr.l %%acc0,%0"
			 : "=d"(r) : "r"(a), "r"(b), "r"(c), "r"(d), "r"(e), "r"(f), "r"(g), "r"(h));
	return r;
}
#else
static inline s32 emac_sat(long long a)
{
	return a > 0x7fffffffLL ? 0x7fffffff : (a < -0x80000000LL ? (s32)0x80000000 : (s32)a);
}
#define P23(a, b) (((long long)(a) * (b)) >> 23)
static inline s32 qmul(s32 a, s32 b)
{
	return emac_sat(P23(a, b) >> 8);
}
static inline s32 qmac2(s32 a, s32 b, s32 c, s32 d)
{
	return emac_sat((P23(a, b) + P23(c, d)) >> 8);
}
static inline s32 qmac4(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h)
{
	return emac_sat((P23(a, b) + P23(c, d) + P23(e, f) + P23(g, h)) >> 8);
}
#endif

#endif
