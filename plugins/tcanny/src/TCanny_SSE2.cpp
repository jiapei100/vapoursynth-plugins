#ifdef VS_TARGET_CPU_X86
#include "TCanny.hpp"

template<typename T>
void copyPlane_sse2(const T * srcp, float * blur, const int width, const int height, const int stride, const int bgStride, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            if (std::is_same<T, uint8_t>::value)
                to_float(Vec4i().load_4uc(srcp + x)).stream(blur + x);
            else if (std::is_same<T, uint16_t>::value)
                to_float(Vec4i().load_4us(srcp + x)).stream(blur + x);
            else
                (Vec4f().load_a(srcp + x) + offset).stream(blur + x);
        }

        srcp += stride;
        blur += bgStride;
    }
}

template void copyPlane_sse2(const uint8_t *, float *, const int, const int, const int, const int, const float) noexcept;
template void copyPlane_sse2(const uint16_t *, float *, const int, const int, const int, const int, const float) noexcept;
template void copyPlane_sse2(const float *, float *, const int, const int, const int, const int, const float) noexcept;

static inline void gaussianBlurH_sse2(float * buffer, float * blur, const float * weights, const int width, const int radius) noexcept {
    weights += radius;

    for (int i = 1; i <= radius; i++) {
        buffer[-i] = buffer[-1 + i];
        buffer[width - 1 + i] = buffer[width - i];
    }

    for (int x = 0; x < width; x += 4) {
        Vec4f sum = zero_4f();

        for (int i = -radius; i <= radius; i++) {
            const Vec4f srcp = Vec4f().load(buffer + x + i);
            sum = mul_add(srcp, weights[i], sum);
        }

        sum.stream(blur + x);
    }
}

template<typename T>
void gaussianBlurV_sse2(const T * __srcp, float * buffer, float * blur, const float * horizontalWeights, const float * verticalWeights,
                        const int width, const int height, const int stride, const int bgStride, const int horizontalRadius, const int verticalRadius, const float offset) noexcept {
    const int diameter = verticalRadius * 2 + 1;
    const T ** _srcp = new const T *[diameter];

    _srcp[verticalRadius] = __srcp;
    for (int i = 1; i <= verticalRadius; i++) {
        _srcp[verticalRadius - i] = _srcp[verticalRadius - 1 + i];
        _srcp[verticalRadius + i] = _srcp[verticalRadius] + stride * i;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            Vec4f sum = zero_4f();

            for (int i = 0; i < diameter; i++) {
                if (std::is_same<T, uint8_t>::value) {
                    const Vec4f srcp = to_float(Vec4i().load_4uc(_srcp[i] + x));
                    sum = mul_add(srcp, verticalWeights[i], sum);
                } else if (std::is_same<T, uint16_t>::value) {
                    const Vec4f srcp = to_float(Vec4i().load_4us(_srcp[i] + x));
                    sum = mul_add(srcp, verticalWeights[i], sum);
                } else {
                    const Vec4f srcp = Vec4f().load_a(_srcp[i] + x);
                    sum = mul_add(srcp + offset, verticalWeights[i], sum);
                }
            }

            sum.store_a(buffer + x);
        }

        gaussianBlurH_sse2(buffer, blur, horizontalWeights, width, horizontalRadius);

        for (int i = 0; i < diameter - 1; i++)
            _srcp[i] = _srcp[i + 1];
        if (y < height - 1 - verticalRadius)
            _srcp[diameter - 1] += stride;
        else if (y > height - 1 - verticalRadius)
            _srcp[diameter - 1] -= stride;
        blur += bgStride;
    }

    delete[] _srcp;
}

template void gaussianBlurV_sse2(const uint8_t *, float *, float *, const float *, const float *, const int, const int, const int, const int, const int, const int, const float) noexcept;
template void gaussianBlurV_sse2(const uint16_t *, float *, float *, const float *, const float *, const int, const int, const int, const int, const int, const int, const float) noexcept;
template void gaussianBlurV_sse2(const float *, float *, float *, const float *, const float *, const int, const int, const int, const int, const int, const int, const float) noexcept;

void detectEdge_sse2(float * blur, float * gradient, unsigned * direction, const int width, const int height, const int stride, const int bgStride,
                     const int mode, const unsigned op) noexcept {
    float * srcpp = blur;
    float * srcp = blur;
    float * srcpn = blur + bgStride;

    srcp[-1] = srcp[0];
    srcp[width] = srcp[width - 1];

    for (int y = 0; y < height; y++) {
        srcpn[-1] = srcpn[0];
        srcpn[width] = srcpn[width - 1];

        for (int x = 0; x < width; x += 4) {
            const Vec4f topLeft = Vec4f().load(srcpp + x - 1);
            const Vec4f top = Vec4f().load_a(srcpp + x);
            const Vec4f topRight = Vec4f().load(srcpp + x + 1);
            const Vec4f left = Vec4f().load(srcp + x - 1);
            const Vec4f right = Vec4f().load(srcp + x + 1);
            const Vec4f bottomLeft = Vec4f().load(srcpn + x - 1);
            const Vec4f bottom = Vec4f().load_a(srcpn + x);
            const Vec4f bottomRight = Vec4f().load(srcpn + x + 1);

            Vec4f gx, gy;

            if (op == 0) {
                gx = right - left;
                gy = top - bottom;
            } else if (op == 1) {
                gx = (topRight + right + bottomRight - topLeft - left - bottomLeft) * 0.5f;
                gy = (topLeft + top + topRight - bottomLeft - bottom - bottomRight) * 0.5f;
            } else if (op == 2) {
                gx = topRight + mul_add(2.f, right, bottomRight) - topLeft - mul_add(2.f, left, bottomLeft);
                gy = topLeft + mul_add(2.f, top, topRight) - bottomLeft - mul_add(2.f, bottom, bottomRight);
            } else {
                gx = mul_add(3.f, topRight, mul_add(10.f, right, 3.f * bottomRight)) - mul_add(3.f, topLeft, mul_add(10.f, left, 3.f * bottomLeft));
                gy = mul_add(3.f, topLeft, mul_add(10.f, top, 3.f * topRight)) - mul_add(3.f, bottomLeft, mul_add(10.f, bottom, 3.f * bottomRight));
            }

            sqrt(mul_add(gx, gx, gy * gy)).stream(gradient + x);

            if (mode == 0) {
                Vec4f dr = atan2(gy, gx);
                dr = if_add(dr < 0.f, dr, M_PIF);

                const Vec4ui bin = Vec4ui(truncate_to_int(mul_add(dr, 4.f * M_1_PIF, 0.5f)));
                select(bin >= 4, zero_128b(), bin).stream(direction + x);
            }
        }

        srcpp = srcp;
        srcp = srcpn;
        if (y < height - 2)
            srcpn += bgStride;
        gradient += bgStride;
        direction += stride;
    }
}

void nonMaximumSuppression_sse2(const unsigned * _direction, float * _gradient, float * blur, const int width, const int height, const int stride, const int bgStride) noexcept {
    _gradient[-1] = _gradient[0];
    _gradient[-1 + bgStride * (height - 1)] = _gradient[bgStride * (height - 1)];
    _gradient[width] = _gradient[width - 1];
    _gradient[width + bgStride * (height - 1)] = _gradient[width - 1 + bgStride * (height - 1)];
    std::copy_n(_gradient - 8, width + 16, _gradient - 8 - bgStride);
    std::copy_n(_gradient - 8 + bgStride * (height - 1), width + 16, _gradient - 8 + bgStride * height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            const Vec4ui direction = Vec4ui().load_a(_direction + x);

            Vec4fb mask = Vec4fb(direction == 0);
            Vec4f gradient = max(Vec4f().load(_gradient + x + 1), Vec4f().load(_gradient + x - 1));
            Vec4f result = gradient & mask;

            mask = Vec4fb(direction == 1);
            gradient = max(Vec4f().load(_gradient + x - bgStride + 1), Vec4f().load(_gradient + x + bgStride - 1));
            result |= gradient & mask;

            mask = Vec4fb(direction == 2);
            gradient = max(Vec4f().load_a(_gradient + x - bgStride), Vec4f().load_a(_gradient + x + bgStride));
            result |= gradient & mask;

            mask = Vec4fb(direction == 3);
            gradient = max(Vec4f().load(_gradient + x - bgStride - 1), Vec4f().load(_gradient + x + bgStride + 1));
            result |= gradient & mask;

            gradient = Vec4f().load_a(_gradient + x);
            select(gradient >= result, gradient, fltLowest).stream(blur + x);
        }

        _direction += stride;
        _gradient += bgStride;
        blur += bgStride;
    }
}

template<typename T> void outputGB_sse2(const float *, T *, const int, const int, const int, const int, const uint16_t, const float) noexcept;

template<>
void outputGB_sse2(const float * blur, uint8_t * dstp, const int width, const int height, const int stride, const int bgStride, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 16) {
            const Vec4i srcp_4i_0 = truncate_to_int(Vec4f().load_a(blur + x) + 0.5f);
            const Vec4i srcp_4i_1 = truncate_to_int(Vec4f().load_a(blur + x + 4) + 0.5f);
            const Vec4i srcp_4i_2 = truncate_to_int(Vec4f().load_a(blur + x + 8) + 0.5f);
            const Vec4i srcp_4i_3 = truncate_to_int(Vec4f().load_a(blur + x + 12) + 0.5f);
            const Vec8s srcp_8s_0 = compress_saturated(srcp_4i_0, srcp_4i_1);
            const Vec8s srcp_8s_1 = compress_saturated(srcp_4i_2, srcp_4i_3);
            const Vec16uc srcp = compress_saturated_s2u(srcp_8s_0, srcp_8s_1);
            srcp.stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<>
void outputGB_sse2(const float * blur, uint16_t * dstp, const int width, const int height, const int stride, const int bgStride, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 8) {
            const Vec4i srcp_4i_0 = truncate_to_int(Vec4f().load_a(blur + x) + 0.5f);
            const Vec4i srcp_4i_1 = truncate_to_int(Vec4f().load_a(blur + x + 4) + 0.5f);
            const Vec8us srcp = compress_saturated_s2u(srcp_4i_0, srcp_4i_1);
            min(srcp, peak).stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<>
void outputGB_sse2(const float * blur, float * dstp, const int width, const int height, const int stride, const int bgStride, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            const Vec4f srcp = Vec4f().load_a(blur + x);
            (srcp - offset).stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<typename T> void binarizeCE_sse2(const float *, T *, const int, const int, const int, const int, const uint16_t, const float, const float) noexcept;

template<>
void binarizeCE_sse2(const float * blur, uint8_t * dstp, const int width, const int height, const int stride, const int bgStride,
                     const uint16_t peak, const float lower, const float upper) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 16) {
            const Vec4ib mask_4ib_0 = Vec4ib(Vec4f().load_a(blur + x) == fltMax);
            const Vec4ib mask_4ib_1 = Vec4ib(Vec4f().load_a(blur + x + 4) == fltMax);
            const Vec4ib mask_4ib_2 = Vec4ib(Vec4f().load_a(blur + x + 8) == fltMax);
            const Vec4ib mask_4ib_3 = Vec4ib(Vec4f().load_a(blur + x + 12) == fltMax);
            const Vec8sb mask_8sb_0 = Vec8sb(compress_saturated(mask_4ib_0, mask_4ib_1));
            const Vec8sb mask_8sb_1 = Vec8sb(compress_saturated(mask_4ib_2, mask_4ib_3));
            const Vec16cb mask = Vec16cb(compress_saturated(mask_8sb_0, mask_8sb_1));
            select(mask, Vec16uc(255), zero_128b()).stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<>
void binarizeCE_sse2(const float * blur, uint16_t * dstp, const int width, const int height, const int stride, const int bgStride,
                     const uint16_t peak, const float lower, const float upper) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 8) {
            const Vec4ib mask_4ib_0 = Vec4ib(Vec4f().load_a(blur + x) == fltMax);
            const Vec4ib mask_4ib_1 = Vec4ib(Vec4f().load_a(blur + x + 4) == fltMax);
            const Vec8sb mask = Vec8sb(compress_saturated(mask_4ib_0, mask_4ib_1));
            select(mask, Vec8us(peak), zero_128b()).stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<>
void binarizeCE_sse2(const float * blur, float * dstp, const int width, const int height, const int stride, const int bgStride,
                     const uint16_t peak, const float lower, const float upper) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            const Vec4fb mask = Vec4f().load_a(blur + x) == fltMax;
            select(mask, Vec4f(upper), Vec4f(lower)).stream(dstp + x);
        }

        blur += bgStride;
        dstp += stride;
    }
}

template<typename T> void discretizeGM_sse2(const float *, T *, const int, const int, const int, const int, const float, const uint16_t, const float) noexcept;

template<>
void discretizeGM_sse2(const float * gradient, uint8_t * dstp, const int width, const int height, const int stride, const int bgStride,
                       const float magnitude, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 16) {
            const Vec4f srcp_4f_0 = Vec4f().load_a(gradient + x);
            const Vec4f srcp_4f_1 = Vec4f().load_a(gradient + x + 4);
            const Vec4f srcp_4f_2 = Vec4f().load_a(gradient + x + 8);
            const Vec4f srcp_4f_3 = Vec4f().load_a(gradient + x + 12);
            const Vec4i srcp_4i_0 = truncate_to_int(mul_add(srcp_4f_0, magnitude, 0.5f));
            const Vec4i srcp_4i_1 = truncate_to_int(mul_add(srcp_4f_1, magnitude, 0.5f));
            const Vec4i srcp_4i_2 = truncate_to_int(mul_add(srcp_4f_2, magnitude, 0.5f));
            const Vec4i srcp_4i_3 = truncate_to_int(mul_add(srcp_4f_3, magnitude, 0.5f));
            const Vec8s srcp_8s_0 = compress_saturated(srcp_4i_0, srcp_4i_1);
            const Vec8s srcp_8s_1 = compress_saturated(srcp_4i_2, srcp_4i_3);
            const Vec16uc srcp = compress_saturated_s2u(srcp_8s_0, srcp_8s_1);
            srcp.stream(dstp + x);
        }

        gradient += bgStride;
        dstp += stride;
    }
}

template<>
void discretizeGM_sse2(const float * gradient, uint16_t * dstp, const int width, const int height, const int stride, const int bgStride,
                       const float magnitude, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 8) {
            const Vec4f srcp_4f_0 = Vec4f().load_a(gradient + x);
            const Vec4f srcp_4f_1 = Vec4f().load_a(gradient + x + 4);
            const Vec4i srcp_4i_0 = truncate_to_int(mul_add(srcp_4f_0, magnitude, 0.5f));
            const Vec4i srcp_4i_1 = truncate_to_int(mul_add(srcp_4f_1, magnitude, 0.5f));
            const Vec8us srcp = compress_saturated_s2u(srcp_4i_0, srcp_4i_1);
            min(srcp, peak).stream(dstp + x);
        }

        gradient += bgStride;
        dstp += stride;
    }
}

template<>
void discretizeGM_sse2(const float * gradient, float * dstp, const int width, const int height, const int stride, const int bgStride,
                       const float magnitude, const uint16_t peak, const float offset) noexcept {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 4) {
            const Vec4f srcp = Vec4f().load_a(gradient + x);
            mul_sub(srcp, magnitude, offset).stream(dstp + x);
        }

        gradient += bgStride;
        dstp += stride;
    }
}
#endif
