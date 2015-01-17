#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

// 2 to the power of 14
#define CONVERTOR 16384
#define INT2FP(VAL) ((VAL) * CONVERTOR)
#define FP2INT(VAL) ((VAL) > 0 ? (((VAL) + (CONVERTOR >> 1)) / CONVERTOR) : (((VAL) - (CONVERTOR >> 1)) / CONVERTOR))
#define FP_MULTIPLY(X,Y) ((((int64_t) (X)) * (Y)) / CONVERTER)
#define FP_DEVIDE(X,Y) ((((int64_t) (X)) * CONVERTER) / (Y))


#endif
