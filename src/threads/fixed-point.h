#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

// 2 to the power of 14
//#define CONVERTOR 16384
//#define INT2FP(VAL) ((VAL) * CONVERTOR)
//#define FP2INT(VAL) ((VAL) > 0 ? (((VAL) + (CONVERTOR >> 1)) / CONVERTOR) : (((VAL) - (CONVERTOR >> 1)) / CONVERTOR))
//#define FP_MULTIPLY(X,Y) ((((int64_t) (X)) * (Y)) / CONVERTOR)
//#define FP_DEVIDE(X,Y) ((((int64_t) (X)) * CONVERTOR) / (Y))

#define FP_F 16384                  /* (1 << 14) */
#define FP2INT(f) ((f) > 0 ? ((f) + (FP_F >> 1)) / FP_F \
                                      : ((f) - (FP_F >> 1)) / FP_F)
#define INT2FP(n) ((n) * FP_F)
#define FP_MULTIPLY(x, y) ((int64_t) (x)) * (y) / FP_F
#define FP_DEVIDE(x, y) ((int64_t) (x)) * FP_F / (y)


#endif
