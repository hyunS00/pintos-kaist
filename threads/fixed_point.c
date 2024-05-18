#include "threads/fixed_point.h"
#define real    int

/* 정수를 실수로 변환: 정수 n에 2^14를 곱함 */
real integer_to_fixed(int n) {
    return n * FIXED_POINT_FACTOR;
}

/* 실수를 0에 가까운 정수로 변환: 실수 x에 2^14로 나눔 */
int fixed_to_toward_zero_integer(real x) {
    return x / FIXED_POINT_FACTOR;
}

/* 실수를 정수로 반올림해 변환: 음수인 경우 x에 2^14/2를 더한 후 2^14로 나눔 양수인 경우 x에 2^14/2를 뺀 후 2^14로 나눔*/
int fixed_to_nearest_integer(real x){
    if(x >= 0)
        return (x + (FIXED_POINT_FACTOR / 2)) / FIXED_POINT_FACTOR;
    return (x - FIXED_POINT_FACTOR / 2) / FIXED_POINT_FACTOR;
}

/* 실수와 정수를 더함 실수 x와 정수 n에 2^14를 곱한 값을 더함 */
real add_fixed_from_integer (real x, int n){
    return x + (n * FIXED_POINT_FACTOR);
}

/* 실수와 정수를 뺌 실수 x와 정수 n에 2^14를 곱한 값을 뺌 */
real minus_fixed_from_integer (real x, int n){
    return x - n * FIXED_POINT_FACTOR;
}

/* 두 실수를 곱함: 실수 x를 64비트 정수로 변환한 후, y와 곱하고 고정 소수점 인자 f로 나눔*/
real multiple_fixed (real x, real y) {
    return (((int64_t) x) * y) / FIXED_POINT_FACTOR;
}

/* 실수 x에 실수 y로 나눔: 고정 소수점 수 x를 64비트 정수로 변환하고 고정 소수점 인자 f를 곱한 후, y로 나눔  */
real divide_fixed (real x, real y) {
    return (((int64_t) x) * FIXED_POINT_FACTOR) / y;
}

// 실수 x에 1을 더함
real add_one_fixed(real x) {
    return add_fixed_from_integer(x, 1);
}