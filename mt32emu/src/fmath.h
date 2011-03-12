#undef FAST_MATH

#ifdef FAST_MATH

inline float fpow2(float x) {
static const float m0 = 1.f;
static const float m1 = 0.693147f;
static const float m2 = 0.240226f;
static const float m3 = 0.0555041f;
static const float m4 = 0.00961812f;
static const float m5 = 0.00133335f;
float f;
int i;
	_asm {
		fld		x
		fist	i
		fild	i		; int(x) x
		mov		eax, i
		fsub			; frac(x)
//	y = ((((0.00133335*f+0.00961812)*f+0.0555041)*f+0.240226)*f+0.693147)*f+1;
		shl		eax, 23
		fld		m5
		fmul	st, st(1)
		fadd	m4
		fmul	st, st(1)
		fadd	m3
		fmul	st, st(1)
		fadd	m2
		fmul	st, st(1)
		fadd	m1
		fmul
		fadd	m0
		fstp	f
		add		eax, f
		and		eax, 7FFFFFFFh
		mov		f, eax
	}
	return f;
}

#else

#include <math.h>

inline float fpow2(float x) {
	return expf(0.69314718f * x);
}

#endif

inline float fsin(float x) {
static const float PI = 3.1415926f;
static const float PI4r = 4 / PI;
static const float sinc[10] = {3.13361941E-7f, -3.65764447E-5f, 0.00249039556f, -0.0807455329f, 0.785398230f, \
							   3.59087057E-6f, -3.25992465E-4f, 0.01585434900f, -0.3084251900f, 1.f};
static const unsigned int sincc[8] = {0, 5, 5, 0, 0, 5, 5, 0};
static const float sinmc[32] = {1.f,  0.f, 0.f,  1.f, -1.f,  0.f,  0.f, -1.f, \
								0.f,  1.f, 1.f,  0.f,  0.f, -1.f, -1.f,  0.f, \
								0.f,  1.f, 0.f,  1.f,  0.f,  1.f,  0.f,  1.f, \
								1.f, -1.f, 1.f, -1.f,  1.f, -1.f,  1.f, -1.f};
	float f, f2, y;
	int i, ic;

	f = x * (4 / PI);
	i = int(f);
	f -= i;
	i &= 7;
	ic = sincc[i];
	f = (f - sinmc[i + 16]) * sinmc[i + 24];
	f2 = f * f;
	y = f * sinmc[i] + sinmc[i + 8];
	y = ((((sinc[ic]*f2 + sinc[ic+1])*f2 + sinc[ic+2])*f2 + sinc[ic+3])*f2 + sinc[ic+4])*y;
	return y;
}

inline float fcos(float x) {
static const float PI = 3.1415926f;
static const float PI4r = 4.f / PI;
static const float sinc[10] = {3.13361941E-7f, -3.65764447E-5f, 0.00249039556f, -0.0807455329f, 0.785398230f, \
							   3.59087057E-6f, -3.25992465E-4f, 0.01585434900f, -0.3084251900f, 1.f};
static const unsigned int sincc[8] = {0, 5, 5, 0, 0, 5, 5, 0};
static const float sinmc[32] = {1.f,  0.f, 0.f,  1.f, -1.f,  0.f,  0.f, -1.f, \
								0.f,  1.f, 1.f,  0.f,  0.f, -1.f, -1.f,  0.f, \
								0.f,  1.f, 0.f,  1.f,  0.f,  1.f,  0.f,  1.f, \
								1.f, -1.f, 1.f, -1.f,  1.f, -1.f,  1.f, -1.f};
	float f, f2, y;
	int i, ic;

	f = (x + PI / 2.f) * (4 / PI);
	i = int(f);
	f -= i;
	i &= 7;
	ic = sincc[i];
	f = (f - sinmc[i + 16]) * sinmc[i + 24];
	f2 = f * f;
	y = f * sinmc[i] + sinmc[i + 8];
	y = ((((sinc[ic]*f2 + sinc[ic+1])*f2 + sinc[ic+2])*f2 + sinc[ic+3])*f2 + sinc[ic+4])*y;
	return y;
}
